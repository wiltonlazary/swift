#ifndef SWIFT_SYNTAX_SYNTAXCOLLECTIONDATA_H
#define SWIFT_SYNTAX_SYNTAXCOLLECTIONDATA_H

#include "swift/Syntax/SyntaxData.h"

namespace swift {
namespace syntax {

template <SyntaxKind CollectionKind, typename ElementType>
class SyntaxCollection;

template <SyntaxKind CollectionKind, typename ElementType>
class SyntaxCollectionData : public SyntaxData {
  friend class SyntaxCollection<CollectionKind, ElementType>;
  std::vector<RC<typename ElementType::DataType>> CachedElements;

  friend struct SyntaxFactory;
  friend class SyntaxData;
  friend class FunctionCallExprSyntaxBuilder;

  SyntaxCollectionData(RC<RawSyntax> Raw, const SyntaxData *Parent = nullptr,
                       CursorIndex IndexInParent = 0)
      : SyntaxData(Raw, Parent, IndexInParent),
        CachedElements(Raw->Layout.size(), nullptr) {
    assert(Raw->Kind == CollectionKind);
  }

  static RC<SyntaxCollectionData<CollectionKind, ElementType>>
  make(RC<RawSyntax> Raw, const SyntaxData *Parent = nullptr,
       CursorIndex IndexInParent = 0) {

    return RC<SyntaxCollectionData<CollectionKind, ElementType>> {
      new SyntaxCollectionData<CollectionKind, ElementType> {
        Raw, Parent, IndexInParent
      }
    };
  }

  static RC<SyntaxCollectionData<CollectionKind, ElementType>> makeBlank() {
    auto Raw = RawSyntax::make(CollectionKind, {}, SourcePresence::Present);
    return make(Raw);
  }

public:
  static bool classof(const SyntaxData *SD) {
    return SD->getKind() == CollectionKind;
  }
};

} // end namespace syntax
} // end namespace swift

#endif
