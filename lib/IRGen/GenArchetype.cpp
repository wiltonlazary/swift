//===--- GenArchetype.cpp - Swift IR Generation for Archetype Types -------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
//  This file implements IR generation for archetype types in Swift.
//
//===----------------------------------------------------------------------===//

#include "GenArchetype.h"

#include "swift/AST/ASTContext.h"
#include "swift/AST/Types.h"
#include "swift/AST/Decl.h"
#include "swift/SIL/SILValue.h"
#include "swift/SIL/TypeLowering.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"

#include "EnumPayload.h"
#include "Explosion.h"
#include "FixedTypeInfo.h"
#include "GenClass.h"
#include "GenHeap.h"
#include "GenMeta.h"
#include "GenOpaque.h"
#include "GenPoly.h"
#include "GenProto.h"
#include "GenType.h"
#include "HeapTypeInfo.h"
#include "IRGenDebugInfo.h"
#include "IRGenFunction.h"
#include "IRGenModule.h"
#include "Linking.h"
#include "ProtocolInfo.h"
#include "ResilientTypeInfo.h"
#include "TypeInfo.h"
#include "WeakTypeInfo.h"

using namespace swift;
using namespace irgen;

llvm::Value *irgen::emitArchetypeTypeMetadataRef(IRGenFunction &IGF,
                                                 CanArchetypeType archetype) {
  // Check for an existing cache entry.
  auto localDataKind = LocalTypeDataKind::forTypeMetadata();
  auto metadata = IGF.tryGetLocalTypeData(archetype, localDataKind);

  // If that's not present, this must be an associated type.
  if (!metadata) {
    assert(!archetype->isPrimary() &&
           "type metadata for primary archetype was not bound in context");

    CanArchetypeType parent(archetype->getParent());
    metadata = emitAssociatedTypeMetadataRef(IGF, parent,
                                             archetype->getAssocType());

    setTypeMetadataName(IGF.IGM, metadata, archetype);

    IGF.setScopedLocalTypeData(archetype, localDataKind, metadata);
  }

  return metadata;
}

namespace {
  struct ConformancePath {
    CanArchetypeType ParentArchetype;
    ProtocolDecl *ParentProtocol;
    CanType Path;
  };
}

static Optional<ConformancePath>
declaresDirectConformance(ProtocolDecl *source,
                          AssociatedTypeDecl *associatedType,
                          ProtocolDecl *target) {
  for (auto &reqt : source->getRequirementSignature()->getRequirements()) {
    if (reqt.getKind() != RequirementKind::Conformance)
      continue;
    if (reqt.getSecondType()->castTo<ProtocolType>()->getDecl() != target)
      continue;
    auto path = reqt.getFirstType()->getCanonicalType();
    if (auto memberType = dyn_cast<DependentMemberType>(path)) {
      if (isa<GenericTypeParamType>(memberType.getBase())) {
        assert(memberType.getBase()->isEqual(source->getSelfInterfaceType()));
        if (memberType->getName() == associatedType->getName())
          return ConformancePath{ CanArchetypeType(), source, path };
      }
    }
  }
  return None;
}

static Optional<ConformancePath>
findConformanceRecursive(ArrayRef<ProtocolDecl*> conformsTo,
                         AssociatedTypeDecl *associatedType,
                         ProtocolDecl *target) {
  for (auto source : conformsTo) {
    // Check the protocol directly.
    if (source != associatedType->getProtocol())
      if (auto result = declaresDirectConformance(source, associatedType,
                                                  target))
        return result;

    // Recurse into implied protocols.
    if (auto result = findConformanceRecursive(source->getInheritedProtocols(),
                                               associatedType, target))
      return result;
  }

  // Give up.
  return None;
}

static Optional<ConformancePath>
findConformanceDeclaration(ArrayRef<ProtocolDecl*> conformsTo,
                           AssociatedTypeDecl *associatedType,
                           ProtocolDecl *target) {
  // Fast path: if the protocol that made this associated type
  // declaration declared the desired conformance, we can use it directly.
  if (auto result = declaresDirectConformance(associatedType->getProtocol(),
                                              associatedType, target))
    return result;

  return findConformanceRecursive(conformsTo, associatedType, target);
}

static ConformancePath
findAccessPathDeclaringConformance(IRGenFunction &IGF,
                                   CanArchetypeType archetype,
                                   ProtocolDecl *protocol) {
  // Consider all the associated type relationships we know about.

  // Use the archetype's parent relationship first if possible.
  if (!archetype->isPrimary()) {
    auto parent = archetype.getParent();
    auto association =
      findConformanceDeclaration(parent->getConformsTo(),
                                 archetype->getAssocType(), protocol);
    if (association) {
      association->ParentArchetype = parent;
      return *association;
    }
  }

  for (auto accessPath : IGF.getArchetypeAccessPaths(archetype)) {
    auto association =
      findConformanceDeclaration(accessPath.BaseType->getConformsTo(),
                                 accessPath.Association, protocol);
    if (association) {
      association->ParentArchetype = accessPath.BaseType;
      return *association;
    }
  }

  llvm_unreachable("no relation found that declares conformance to target");
}

namespace {

/// Common type implementation details for all archetypes.
class ArchetypeTypeInfoBase {
protected:
  unsigned NumStoredProtocols;
  ProtocolEntry *StoredProtocolsBuffer;

  ArchetypeTypeInfoBase(void *protocolsBuffer,
                        ArrayRef<ProtocolEntry> protocols)
    : NumStoredProtocols(protocols.size()),
      StoredProtocolsBuffer(reinterpret_cast<ProtocolEntry*>(protocolsBuffer))
  {
    for (unsigned i = 0, e = protocols.size(); i != e; ++i) {
      ::new (&StoredProtocolsBuffer[i]) ProtocolEntry(protocols[i]);
    }
  }

public:
  unsigned getNumStoredProtocols() const {
    return NumStoredProtocols;
  }

  ArrayRef<ProtocolEntry> getStoredProtocols() const {
    return llvm::makeArrayRef(StoredProtocolsBuffer, getNumStoredProtocols());
  }

  /// Return the witness table that's been set for this type.
  llvm::Value *getWitnessTable(IRGenFunction &IGF,
                               CanArchetypeType archetype,
                               unsigned which) const {
    assert(which < getNumStoredProtocols());
    auto protocol = archetype->getConformsTo()[which];
    auto localDataKind =
      LocalTypeDataKind::forAbstractProtocolWitnessTable(protocol);

    // Check for an existing cache entry.
    auto wtable = IGF.tryGetLocalTypeData(archetype, localDataKind);
    if (wtable) return wtable;

    // It can happen with class constraints that Sema will consider a
    // constraint to be abstract, but the minimized signature will
    // eliminate it as concrete.  Handle this by performing a concrete
    // lookup.
    // TODO: maybe Sema shouldn't ever do this?
    if (Type classBound = archetype->getSuperclass()) {
      auto conformance =
        IGF.IGM.getSwiftModule()->lookupConformance(classBound, protocol,
                                                    nullptr);
      if (conformance && conformance->isConcrete()) {
        return emitWitnessTableRef(IGF, archetype, *conformance);
      }
    }

    // If that's not present, this conformance must be implied by some
    // associated-type relationship.

    // First, find the right access path.
    auto accessPath =
      findAccessPathDeclaringConformance(IGF, archetype, protocol);

    // Get the metadata for the parent.
    llvm::Value *parentMetadata =
      emitArchetypeTypeMetadataRef(IGF, accessPath.ParentArchetype);

    // Get the conformance of the parent to the protocol which declares
    // the associated conformance.
    llvm::Value *parentWTable =
      emitArchetypeWitnessTableRef(IGF, accessPath.ParentArchetype,
                                   accessPath.ParentProtocol);

    // Get the metadata for the associated type, i.e. this type.
    auto archetypeMetadata = emitArchetypeTypeMetadataRef(IGF, archetype);

    // Call the accessor.
    wtable = emitAssociatedTypeWitnessTableRef(IGF, parentMetadata,
                                               parentWTable,
                                               accessPath.ParentProtocol,
                                               accessPath.Path,
                                               archetypeMetadata,
                                               protocol);

    setProtocolWitnessTableName(IGF.IGM, wtable, archetype, protocol);

    IGF.setScopedLocalTypeData(archetype, localDataKind, wtable);

    return wtable;
  }
};

/// A type implementation for an ArchetypeType, otherwise known as a
/// type variable: for example, Self in a protocol declaration, or T
/// in a generic declaration like foo<T>(x : T) -> T.  The critical
/// thing here is that performing an operation involving archetypes
/// is dependent on the witness binding we can see.
class OpaqueArchetypeTypeInfo
  : public ResilientTypeInfo<OpaqueArchetypeTypeInfo>,
    public ArchetypeTypeInfoBase
{
  OpaqueArchetypeTypeInfo(llvm::Type *type,
                          ArrayRef<ProtocolEntry> protocols)
    : ResilientTypeInfo(type),
      ArchetypeTypeInfoBase(this + 1, protocols)
  {}

public:
  static const OpaqueArchetypeTypeInfo *create(llvm::Type *type,
                                         ArrayRef<ProtocolEntry> protocols) {
    void *buffer = operator new(sizeof(OpaqueArchetypeTypeInfo)
                                + protocols.size() * sizeof(ProtocolEntry));
    return ::new (buffer) OpaqueArchetypeTypeInfo(type, protocols);
  }
};

/// A type implementation for a class archetype, that is, an archetype
/// bounded by a class protocol constraint. These archetypes can be
/// represented by a refcounted pointer instead of an opaque value buffer.
/// If ObjC interop is disabled, we can use Swift refcounting entry
/// points, otherwise we have to use the unknown ones.
class ClassArchetypeTypeInfo
  : public HeapTypeInfo<ClassArchetypeTypeInfo>,
    public ArchetypeTypeInfoBase
{
  ReferenceCounting RefCount;

  ClassArchetypeTypeInfo(llvm::PointerType *storageType,
                         Size size, const SpareBitVector &spareBits,
                         Alignment align,
                         ArrayRef<ProtocolEntry> protocols,
                         ReferenceCounting refCount)
    : HeapTypeInfo(storageType, size, spareBits, align),
      ArchetypeTypeInfoBase(this + 1, protocols),
      RefCount(refCount)
  {}

public:
  static const ClassArchetypeTypeInfo *create(llvm::PointerType *storageType,
                                         Size size, const SpareBitVector &spareBits,
                                         Alignment align,
                                         ArrayRef<ProtocolEntry> protocols,
                                         ReferenceCounting refCount) {
    void *buffer = operator new(sizeof(ClassArchetypeTypeInfo)
                                  + protocols.size() * sizeof(ProtocolEntry));
    return ::new (buffer)
      ClassArchetypeTypeInfo(storageType, size, spareBits, align,
                             protocols, refCount);
  }

  ReferenceCounting getReferenceCounting() const {
    return RefCount;
  }
};

class FixedSizeArchetypeTypeInfo
  : public PODSingleScalarTypeInfo<FixedSizeArchetypeTypeInfo, LoadableTypeInfo>,
    public ArchetypeTypeInfoBase
{
  FixedSizeArchetypeTypeInfo(llvm::Type *type, Size size, Alignment align,
                             const SpareBitVector &spareBits,
                             ArrayRef<ProtocolEntry> protocols)
      : PODSingleScalarTypeInfo(type, size, spareBits, align),
        ArchetypeTypeInfoBase(this + 1, protocols) {}

public:
  static const FixedSizeArchetypeTypeInfo *
  create(llvm::Type *type, Size size, Alignment align,
         const SpareBitVector &spareBits, ArrayRef<ProtocolEntry> protocols) {
    void *buffer = operator new(sizeof(FixedSizeArchetypeTypeInfo) +
                                protocols.size() * sizeof(ProtocolEntry));
    return ::new (buffer)
        FixedSizeArchetypeTypeInfo(type, size, align, spareBits, protocols);
  }
};

} // end anonymous namespace

/// Return the ArchetypeTypeInfoBase information from the TypeInfo for any
/// archetype.
static const ArchetypeTypeInfoBase &
getArchetypeInfo(IRGenFunction &IGF, CanArchetypeType t, const TypeInfo &ti) {
  LayoutConstraint LayoutInfo = t->getLayoutConstraint();
  if (t->requiresClass() || (LayoutInfo && LayoutInfo->isRefCountedObject()))
    return ti.as<ClassArchetypeTypeInfo>();
  if (LayoutInfo && LayoutInfo->isFixedSizeTrivial())
    return ti.as<FixedSizeArchetypeTypeInfo>();
  // TODO: Handle LayoutConstraintInfo::Trivial
  return ti.as<OpaqueArchetypeTypeInfo>();
}

/// Emit a single protocol witness table reference.
llvm::Value *irgen::emitArchetypeWitnessTableRef(IRGenFunction &IGF,
                                                 CanArchetypeType archetype,
                                                 ProtocolDecl *proto) {
  assert(Lowering::TypeConverter::protocolRequiresWitnessTable(proto) &&
         "looking up witness table for protocol that doesn't have one");

  // The following approach assumes that a protocol will only appear in
  // an archetype's conformsTo array if the archetype is either explicitly
  // constrained to conform to that protocol (in which case we should have
  // a cache entry for it) or there's an associated type declaration with
  // that protocol listed as a direct requirement.

  // Check immediately for an existing cache entry.
  auto wtable = IGF.tryGetLocalTypeData(archetype,
                  LocalTypeDataKind::forAbstractProtocolWitnessTable(proto));
  if (wtable) return wtable;

  // Otherwise, find the best path from one of the protocols directly
  // conformed to by the protocol, then get that conformance.
  // TODO: this isn't necessarily optimal if the direct conformance isn't
  // concretely available; we really ought to be comparing the full paths
  // to this conformance from concrete sources.
  auto &archTI = getArchetypeInfo(IGF, archetype,
                                  IGF.getTypeInfoForLowered(archetype));
  wtable = emitImpliedWitnessTableRef(IGF, archTI.getStoredProtocols(), proto,
    [&](unsigned originIndex) -> llvm::Value* {
      return archTI.getWitnessTable(IGF, archetype, originIndex);
    });
  return wtable;
}

llvm::Value *irgen::emitAssociatedTypeMetadataRef(IRGenFunction &IGF,
                                                  CanArchetypeType origin,
                                               AssociatedTypeDecl *associate) {
  // Find the conformance of the origin to the associated type's protocol.
  llvm::Value *wtable = emitArchetypeWitnessTableRef(IGF, origin,
                                                     associate->getProtocol());

  // Find the origin's type metadata.
  llvm::Value *originMetadata = emitArchetypeTypeMetadataRef(IGF, origin);

  return emitAssociatedTypeMetadataRef(IGF, originMetadata, wtable, associate);
}

const TypeInfo *TypeConverter::convertArchetypeType(ArchetypeType *archetype) {
  assert(isExemplarArchetype(archetype) && "lowering non-exemplary archetype");

  // Compute layouts for the protocols we ascribe to.
  SmallVector<ProtocolEntry, 4> protocols;
  for (auto protocol : archetype->getConformsTo()) {
    const ProtocolInfo &impl = IGM.getProtocolInfo(protocol);
    protocols.push_back(ProtocolEntry(protocol, impl));
  }

  LayoutConstraint LayoutInfo = archetype->getLayoutConstraint();

  // If the archetype is class-constrained, use a class pointer
  // representation.
  if (archetype->requiresClass() ||
      (LayoutInfo && LayoutInfo->isRefCountedObject())) {
    ReferenceCounting refcount;
    llvm::PointerType *reprTy;

    if (!IGM.ObjCInterop) {
      refcount = ReferenceCounting::Native;
      reprTy = IGM.RefCountedPtrTy;
    } else {
      refcount = ReferenceCounting::Unknown;
      reprTy = IGM.UnknownRefCountedPtrTy;
    }

    // If the archetype has a superclass constraint, it has at least the
    // retain semantics of its superclass, and it can be represented with
    // the supertype's pointer type.
    if (Type super = archetype->getSuperclass()) {
      ClassDecl *superClass = super->getClassOrBoundGenericClass();
      refcount = getReferenceCountingForClass(IGM, superClass);

      auto &superTI = IGM.getTypeInfoForUnlowered(super);
      reprTy = cast<llvm::PointerType>(superTI.StorageType);
    }

    // As a hack, assume class archetypes never have spare bits. There's a
    // corresponding hack in MultiPayloadEnumImplStrategy::completeEnumTypeLayout
    // to ignore spare bits of dependent-typed payloads.
    auto spareBits =
      SpareBitVector::getConstant(IGM.getPointerSize().getValueInBits(), false);

    return ClassArchetypeTypeInfo::create(reprTy,
                                      IGM.getPointerSize(),
                                      spareBits,
                                      IGM.getPointerAlignment(),
                                      protocols, refcount);
  }

  // If the archetype is trivial fixed-size layout-constrained, use a fixed size
  // representation.
  if (LayoutInfo && LayoutInfo->isFixedSizeTrivial()) {
    Size size(LayoutInfo->getTrivialSizeInBytes());
    Alignment align(LayoutInfo->getTrivialSizeInBytes());
    auto spareBits =
      SpareBitVector::getConstant(size.getValueInBits(), false);
    // Get an integer type of the required size.
    auto ProperlySizedIntTy = SILType::getBuiltinIntegerType(
        size.getValueInBits(), IGM.getSwiftModule()->getASTContext());
    auto storageType = IGM.getStorageType(ProperlySizedIntTy);
    return FixedSizeArchetypeTypeInfo::create(storageType, size, align,
                                              spareBits, protocols);
  }

  // If the archetype is a trivial layout-constrained, use a POD
  // representation. This type is not loadable, but it is known
  // to be a POD.
  if (LayoutInfo && LayoutInfo->isAddressOnlyTrivial()) {
    // TODO: Create NonFixedSizeArchetypeTypeInfo and return it.
  }

  // Otherwise, for now, always use an opaque indirect type.
  llvm::Type *storageType = IGM.OpaquePtrTy->getElementType();
  return OpaqueArchetypeTypeInfo::create(storageType, protocols);
}

static void setMetadataRef(IRGenFunction &IGF,
                           ArchetypeType *archetype,
                           llvm::Value *metadata) {
  assert(metadata->getType() == IGF.IGM.TypeMetadataPtrTy);
  IGF.setUnscopedLocalTypeData(CanType(archetype),
                               LocalTypeDataKind::forTypeMetadata(),
                               metadata);
}

static void setWitnessTable(IRGenFunction &IGF,
                            ArchetypeType *archetype,
                            unsigned protocolIndex,
                            llvm::Value *wtable) {
  assert(wtable->getType() == IGF.IGM.WitnessTablePtrTy);
  assert(protocolIndex < archetype->getConformsTo().size());
  auto protocol = archetype->getConformsTo()[protocolIndex];
  IGF.setUnscopedLocalTypeData(CanType(archetype),
                  LocalTypeDataKind::forAbstractProtocolWitnessTable(protocol),
                               wtable);
}

/// Inform IRGenFunction that the given archetype has the given value
/// witness value within this scope.
void IRGenFunction::bindArchetype(ArchetypeType *archetype,
                                  llvm::Value *metadata,
                                  ArrayRef<llvm::Value*> wtables) {
  // Set the metadata pointer.
  setTypeMetadataName(IGM, metadata, CanType(archetype));
  setMetadataRef(*this, archetype, metadata);

  // Set the protocol witness tables.

  unsigned wtableI = 0;
  for (unsigned i = 0, e = archetype->getConformsTo().size(); i != e; ++i) {
    auto proto = archetype->getConformsTo()[i];
    if (!Lowering::TypeConverter::protocolRequiresWitnessTable(proto))
      continue;
    auto wtable = wtables[wtableI++];
    setProtocolWitnessTableName(IGM, wtable, CanType(archetype), proto);
    setWitnessTable(*this, archetype, i, wtable);
  }
  assert(wtableI == wtables.size());
}

llvm::Value *irgen::emitDynamicTypeOfOpaqueArchetype(IRGenFunction &IGF,
                                                     Address addr,
                                                     SILType type) {
  auto archetype = type.castTo<ArchetypeType>();

  // Acquire the archetype's static metadata.
  llvm::Value *metadata = emitArchetypeTypeMetadataRef(IGF, archetype);
  return IGF.Builder.CreateCall(IGF.IGM.getGetDynamicTypeFn(),
                                {addr.getAddress(), metadata,
                                 llvm::ConstantInt::get(IGF.IGM.Int1Ty, 0)});
}
