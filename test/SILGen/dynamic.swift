// RUN: rm -rf %t && mkdir -p %t
// RUN: %build-silgen-test-overlays

// RUN: %target-swift-frontend(mock-sdk: -sdk %S/Inputs -I %t) -Xllvm -new-mangling-for-tests -Xllvm -sil-full-demangle -primary-file %s %S/Inputs/dynamic_other.swift -emit-silgen | %FileCheck %s
// RUN: %target-swift-frontend(mock-sdk: -sdk %S/Inputs -I %t) -Xllvm -sil-full-demangle -primary-file %s %S/Inputs/dynamic_other.swift -emit-sil -verify

// REQUIRES: objc_interop

import Foundation
import gizmo

class Foo: Proto {
  // Not objc or dynamic, so only a vtable entry
  init(native: Int) {}
  func nativeMethod() {}
  var nativeProp: Int = 0
  subscript(native native: Int) -> Int {
    get { return native }
    set {}
  }

  // @objc, so it has an ObjC entry point but can also be dispatched
  // by vtable
  @objc init(objc: Int) {}
  @objc func objcMethod() {}
  @objc var objcProp: Int = 0
  @objc subscript(objc objc: AnyObject) -> Int {
    get { return 0 }
    set {}
  }

  // dynamic, so it has only an ObjC entry point
  dynamic init(dynamic: Int) {}
  dynamic func dynamicMethod() {}
  dynamic var dynamicProp: Int = 0
  dynamic subscript(dynamic dynamic: Int) -> Int {
    get { return dynamic }
    set {}
  }

  func overriddenByDynamic() {}

  @NSManaged var managedProp: Int
}

protocol Proto {
  func nativeMethod()
  var nativeProp: Int { get set }
  subscript(native native: Int) -> Int { get set }

  func objcMethod()
  var objcProp: Int { get set }
  subscript(objc objc: AnyObject) -> Int { get set }

  func dynamicMethod()
  var dynamicProp: Int { get set }
  subscript(dynamic dynamic: Int) -> Int { get set }
}

// ObjC entry points for @objc and dynamic entry points

// normal and @objc initializing ctors can be statically dispatched
// CHECK-LABEL: sil hidden @_T07dynamic3FooC{{.*}}tcfC
// CHECK:         function_ref @_T07dynamic3FooC{{.*}}tcfc

// CHECK-LABEL: sil hidden @_T07dynamic3FooC{{.*}}tcfC
// CHECK:         function_ref @_T07dynamic3FooC{{.*}}tcfc

// CHECK-LABEL: sil hidden [thunk] @_T07dynamic3{{[_0-9a-zA-Z]*}}fcTo
// CHECK-LABEL: sil hidden [thunk] @_T07dynamic3FooC10objcMethod{{[_0-9a-zA-Z]*}}FTo
// CHECK-LABEL: sil hidden [transparent] [thunk] @_T07dynamic3FooC8objcPropSifgTo
// CHECK-LABEL: sil hidden [transparent] [thunk] @_T07dynamic3FooC8objcPropSifsTo
// CHECK-LABEL: sil hidden [thunk] @_T07dynamic3FooC9subscriptSis9AnyObject_p4objc_tcfgTo
// CHECK-LABEL: sil hidden [thunk] @_T07dynamic3FooC9subscriptSis9AnyObject_p4objc_tcfsTo

// TODO: dynamic initializing ctor must be objc dispatched
// CHECK-LABEL: sil hidden @_T07dynamic3{{[_0-9a-zA-Z]*}}fC
// CHECK:         function_ref @_T07dynamic3{{[_0-9a-zA-Z]*}}fcTD
// CHECK-LABEL: sil shared [transparent] [thunk] @_T07dynamic3{{[_0-9a-zA-Z]*}}fcTD
// CHECK:         class_method [volatile] {{%.*}} : $Foo, #Foo.init!initializer.1.foreign :

// CHECK-LABEL: sil hidden [thunk] @_T07dynamic3{{[_0-9a-zA-Z]*}}fcTo
// CHECK-LABEL: sil hidden [thunk] @_T07dynamic3FooC0A6Method{{[_0-9a-zA-Z]*}}FTo
// CHECK-LABEL: sil hidden [transparent] [thunk] @_T07dynamic3FooC0A4PropSifgTo
// CHECK-LABEL: sil hidden [transparent] [thunk] @_T07dynamic3FooC0A4PropSifsTo
// CHECK-LABEL: sil hidden [thunk] @_T07dynamic3FooC9subscriptSiSiAA_tcfgTo
// CHECK-LABEL: sil hidden [thunk] @_T07dynamic3FooC9subscriptSiSiAA_tcfsTo

// Protocol witnesses use best appropriate dispatch

// Native witnesses use vtable dispatch:
// CHECK-LABEL: sil hidden [transparent] [thunk] @_T07dynamic3FooCAA5ProtoAaaDP12nativeMethod{{[_0-9a-zA-Z]*}}FTW
// CHECK:         class_method {{%.*}} : $Foo, #Foo.nativeMethod!1 :
// CHECK-LABEL: sil hidden [transparent] [thunk] @_T07dynamic3FooCAA5ProtoAaaDP10nativePropSifgTW
// CHECK:         class_method {{%.*}} : $Foo, #Foo.nativeProp!getter.1 :
// CHECK-LABEL: sil hidden [transparent] [thunk] @_T07dynamic3FooCAA5ProtoAaaDP10nativePropSifsTW
// CHECK:         class_method {{%.*}} : $Foo, #Foo.nativeProp!setter.1 :
// CHECK-LABEL: sil hidden [transparent] [thunk] @_T07dynamic3FooCAA5ProtoAaaDP9subscriptSiSi6native_tcfgTW
// CHECK:         class_method {{%.*}} : $Foo, #Foo.subscript!getter.1 :
// CHECK-LABEL: sil hidden [transparent] [thunk] @_T07dynamic3FooCAA5ProtoAaaDP9subscriptSiSi6native_tcfsTW
// CHECK:         class_method {{%.*}} : $Foo, #Foo.subscript!setter.1 :

// @objc witnesses use vtable dispatch:
// CHECK-LABEL: sil hidden [transparent] [thunk] @_T07dynamic3FooCAA5ProtoAaaDP10objcMethod{{[_0-9a-zA-Z]*}}FTW
// CHECK:         class_method {{%.*}} : $Foo, #Foo.objcMethod!1 :
// CHECK-LABEL: sil hidden [transparent] [thunk] @_T07dynamic3FooCAA5ProtoAaaDP8objcPropSifgTW
// CHECK:         class_method {{%.*}} : $Foo, #Foo.objcProp!getter.1 :
// CHECK-LABEL: sil hidden [transparent] [thunk] @_T07dynamic3FooCAA5ProtoAaaDP8objcPropSifsTW
// CHECK:         class_method {{%.*}} : $Foo, #Foo.objcProp!setter.1 :
// CHECK-LABEL: sil hidden [transparent] [thunk] @_T07dynamic3FooCAA5ProtoAaaDP9subscriptSis9AnyObject_p4objc_tcfgTW
// CHECK:         class_method {{%.*}} : $Foo, #Foo.subscript!getter.1 :
// CHECK-LABEL: sil hidden [transparent] [thunk] @_T07dynamic3FooCAA5ProtoAaaDP9subscriptSis9AnyObject_p4objc_tcfsTW
// CHECK:         class_method {{%.*}} : $Foo, #Foo.subscript!setter.1 :

// Dynamic witnesses use objc dispatch:
// CHECK-LABEL: sil hidden [transparent] [thunk] @_T07dynamic3FooCAA5ProtoAaaDP0A6Method{{[_0-9a-zA-Z]*}}FTW
// CHECK:         function_ref @_T07dynamic3FooC0A6Method{{[_0-9a-zA-Z]*}}FTD
// CHECK-LABEL: sil shared [transparent] [thunk] @_T07dynamic3FooC0A6Method{{[_0-9a-zA-Z]*}}FTD
// CHECK:         class_method [volatile] {{%.*}} : $Foo, #Foo.dynamicMethod!1.foreign :

// CHECK-LABEL: sil hidden [transparent] [thunk] @_T07dynamic3FooCAA5ProtoAaaDP0A4PropSifgTW
// CHECK:         function_ref @_T07dynamic3FooC0A4PropSifgTD
// CHECK-LABEL: sil shared [transparent] [thunk] @_T07dynamic3FooC0A4PropSifgTD
// CHECK:         class_method [volatile] {{%.*}} : $Foo, #Foo.dynamicProp!getter.1.foreign :

// CHECK-LABEL: sil hidden [transparent] [thunk] @_T07dynamic3FooCAA5ProtoAaaDP0A4PropSifsTW
// CHECK:         function_ref @_T07dynamic3FooC0A4PropSifsTD
// CHECK-LABEL: sil shared [transparent] [thunk] @_T07dynamic3FooC0A4PropSifsTD
// CHECK:         class_method [volatile] {{%.*}} : $Foo, #Foo.dynamicProp!setter.1.foreign :

// CHECK-LABEL: sil hidden [transparent] [thunk] @_T07dynamic3FooCAA5ProtoAaaDP9subscriptSiSiAA_tcfgTW
// CHECK:         function_ref @_T07dynamic3FooC9subscriptSiSiAA_tcfgTD
// CHECK-LABEL: sil shared [transparent] [thunk] @_T07dynamic3FooC9subscriptSiSiAA_tcfgTD
// CHECK:         class_method [volatile] {{%.*}} : $Foo, #Foo.subscript!getter.1.foreign :

// CHECK-LABEL: sil hidden [transparent] [thunk] @_T07dynamic3FooCAA5ProtoAaaDP9subscriptSiSiAA_tcfsTW
// CHECK:         function_ref @_T07dynamic3FooC9subscriptSiSiAA_tcfsTD
// CHECK-LABEL: sil shared [transparent] [thunk] @_T07dynamic3FooC9subscriptSiSiAA_tcfsTD
// CHECK:         class_method [volatile] {{%.*}} : $Foo, #Foo.subscript!setter.1.foreign :

// Superclass dispatch
class Subclass: Foo {
  // Native and objc methods can directly reference super members
  override init(native: Int) {
    super.init(native: native)
  }
  // CHECK-LABEL: sil hidden @_T07dynamic8SubclassC{{[_0-9a-zA-Z]*}}fC
  // CHECK:         function_ref @_T07dynamic8SubclassC{{[_0-9a-zA-Z]*}}fc

  override func nativeMethod() {
    super.nativeMethod()
  }
  // CHECK-LABEL: sil hidden @_T07dynamic8SubclassC12nativeMethod{{[_0-9a-zA-Z]*}}F
  // CHECK:         function_ref @_T07dynamic3FooC12nativeMethodyyF : $@convention(method) (@guaranteed Foo) -> ()

  override var nativeProp: Int {
    get { return super.nativeProp }
    // CHECK-LABEL: sil hidden @_T07dynamic8SubclassC10nativePropSifg
    // CHECK:         function_ref @_T07dynamic3FooC10nativePropSifg : $@convention(method) (@guaranteed Foo) -> Int
    set { super.nativeProp = newValue }
    // CHECK-LABEL: sil hidden @_T07dynamic8SubclassC10nativePropSifs
    // CHECK:         function_ref @_T07dynamic3FooC10nativePropSifs : $@convention(method) (Int, @guaranteed Foo) -> ()
  }

  override subscript(native native: Int) -> Int {
    get { return super[native: native] }
    // CHECK-LABEL: sil hidden @_T07dynamic8SubclassC9subscriptSiSi6native_tcfg
    // CHECK:         function_ref @_T07dynamic3FooC9subscriptSiSi6native_tcfg : $@convention(method) (Int, @guaranteed Foo) -> Int
    set { super[native: native] = newValue }
    // CHECK-LABEL: sil hidden @_T07dynamic8SubclassC9subscriptSiSi6native_tcfs
    // CHECK:         function_ref @_T07dynamic3FooC9subscriptSiSi6native_tcfs : $@convention(method) (Int, Int, @guaranteed Foo) -> ()
  }

  override init(objc: Int) {
    super.init(objc: objc)
  }
  // CHECK-LABEL: sil hidden @_T07dynamic8SubclassCACSi4objc_tcfc
  // CHECK:         function_ref @_T07dynamic3FooCACSi4objc_tcfc : $@convention(method) (Int, @owned Foo) -> @owned Foo

  override func objcMethod() {
    super.objcMethod()
  }
  // CHECK-LABEL: sil hidden @_T07dynamic8SubclassC10objcMethod{{[_0-9a-zA-Z]*}}F
  // CHECK:         function_ref @_T07dynamic3FooC10objcMethodyyF : $@convention(method) (@guaranteed Foo) -> ()

  override var objcProp: Int {
    get { return super.objcProp }
    // CHECK-LABEL: sil hidden @_T07dynamic8SubclassC8objcPropSifg
    // CHECK:         function_ref @_T07dynamic3FooC8objcPropSifg : $@convention(method) (@guaranteed Foo) -> Int
    set { super.objcProp = newValue }
    // CHECK-LABEL: sil hidden @_T07dynamic8SubclassC8objcPropSifs
    // CHECK:         function_ref @_T07dynamic3FooC8objcPropSifs : $@convention(method) (Int, @guaranteed Foo) -> ()
  }

  override subscript(objc objc: AnyObject) -> Int {
    get { return super[objc: objc] }
    // CHECK-LABEL: sil hidden @_T07dynamic8SubclassC9subscriptSis9AnyObject_p4objc_tcfg
    // CHECK:         function_ref @_T07dynamic3FooC9subscriptSis9AnyObject_p4objc_tcfg : $@convention(method) (@owned AnyObject, @guaranteed Foo) -> Int
    set { super[objc: objc] = newValue }
    // CHECK-LABEL: sil hidden @_T07dynamic8SubclassC9subscriptSis9AnyObject_p4objc_tcfs
    // CHECK:         function_ref @_T07dynamic3FooC9subscriptSis9AnyObject_p4objc_tcfs : $@convention(method) (Int, @owned AnyObject, @guaranteed Foo) -> ()
  }

  // Dynamic methods are super-dispatched by objc_msgSend
  override init(dynamic: Int) {
    super.init(dynamic: dynamic)
  }
  // CHECK-LABEL: sil hidden @_T07dynamic8SubclassC{{[_0-9a-zA-Z]*}}fc
  // CHECK:         super_method [volatile] {{%.*}} : $Subclass, #Foo.init!initializer.1.foreign :

  override func dynamicMethod() {
    super.dynamicMethod()
  }
  // CHECK-LABEL: sil hidden @_T07dynamic8SubclassC0A6Method{{[_0-9a-zA-Z]*}}F
  // CHECK:         super_method [volatile] {{%.*}} : $Subclass, #Foo.dynamicMethod!1.foreign :

  override var dynamicProp: Int {
    get { return super.dynamicProp }
    // CHECK-LABEL: sil hidden @_T07dynamic8SubclassC0A4PropSifg
    // CHECK:         super_method [volatile] {{%.*}} : $Subclass, #Foo.dynamicProp!getter.1.foreign :
    set { super.dynamicProp = newValue }
    // CHECK-LABEL: sil hidden @_T07dynamic8SubclassC0A4PropSifs
    // CHECK:         super_method [volatile] {{%.*}} : $Subclass, #Foo.dynamicProp!setter.1.foreign :
  }

  override subscript(dynamic dynamic: Int) -> Int {
    get { return super[dynamic: dynamic] }
    // CHECK-LABEL: sil hidden @_T07dynamic8SubclassC9subscriptSiSiAA_tcfg
    // CHECK:         super_method [volatile] {{%.*}} : $Subclass, #Foo.subscript!getter.1.foreign :
    set { super[dynamic: dynamic] = newValue }
    // CHECK-LABEL: sil hidden @_T07dynamic8SubclassC9subscriptSiSiAA_tcfs
    // CHECK:         super_method [volatile] {{%.*}} : $Subclass, #Foo.subscript!setter.1.foreign :
  }

  dynamic override func overriddenByDynamic() {}
}

// CHECK-LABEL: sil hidden @_T07dynamic20nativeMethodDispatchyyF : $@convention(thin) () -> ()
func nativeMethodDispatch() {
  // CHECK: function_ref @_T07dynamic3{{[_0-9a-zA-Z]*}}fC
  let c = Foo(native: 0)
  // CHECK: class_method {{%.*}} : $Foo, #Foo.nativeMethod!1 :
  c.nativeMethod()
  // CHECK: class_method {{%.*}} : $Foo, #Foo.nativeProp!getter.1 :
  let x = c.nativeProp
  // CHECK: class_method {{%.*}} : $Foo, #Foo.nativeProp!setter.1 :
  c.nativeProp = x
  // CHECK: class_method {{%.*}} : $Foo, #Foo.subscript!getter.1 :
  let y = c[native: 0]
  // CHECK: class_method {{%.*}} : $Foo, #Foo.subscript!setter.1 :
  c[native: 0] = y
}

// CHECK-LABEL: sil hidden @_T07dynamic18objcMethodDispatchyyF : $@convention(thin) () -> ()
func objcMethodDispatch() {
  // CHECK: function_ref @_T07dynamic3{{[_0-9a-zA-Z]*}}fC
  let c = Foo(objc: 0)
  // CHECK: class_method {{%.*}} : $Foo, #Foo.objcMethod!1 :
  c.objcMethod()
  // CHECK: class_method {{%.*}} : $Foo, #Foo.objcProp!getter.1 :
  let x = c.objcProp
  // CHECK: class_method {{%.*}} : $Foo, #Foo.objcProp!setter.1 :
  c.objcProp = x
  // CHECK: class_method {{%.*}} : $Foo, #Foo.subscript!getter.1 :
  let y = c[objc: 0 as NSNumber]
  // CHECK: class_method {{%.*}} : $Foo, #Foo.subscript!setter.1 :
  c[objc: 0 as NSNumber] = y
}

// CHECK-LABEL: sil hidden @_T07dynamic0A14MethodDispatchyyF : $@convention(thin) () -> ()
func dynamicMethodDispatch() {
  // CHECK: function_ref @_T07dynamic3{{[_0-9a-zA-Z]*}}fC
  let c = Foo(dynamic: 0)
  // CHECK: class_method [volatile] {{%.*}} : $Foo, #Foo.dynamicMethod!1.foreign 
  c.dynamicMethod()
  // CHECK: class_method [volatile] {{%.*}} : $Foo, #Foo.dynamicProp!getter.1.foreign
  let x = c.dynamicProp
  // CHECK: class_method [volatile] {{%.*}} : $Foo, #Foo.dynamicProp!setter.1.foreign
  c.dynamicProp = x
  // CHECK: class_method [volatile] {{%.*}} : $Foo, #Foo.subscript!getter.1.foreign
  let y = c[dynamic: 0]
  // CHECK: class_method [volatile] {{%.*}} : $Foo, #Foo.subscript!setter.1.foreign
  c[dynamic: 0] = y
}

// CHECK-LABEL: sil hidden @_T07dynamic15managedDispatchyAA3FooCF
func managedDispatch(_ c: Foo) {
  // CHECK: class_method [volatile] {{%.*}} : $Foo, #Foo.managedProp!getter.1.foreign 
  let x = c.managedProp
  // CHECK: class_method [volatile] {{%.*}} : $Foo, #Foo.managedProp!setter.1.foreign
  c.managedProp = x
}

// CHECK-LABEL: sil hidden @_T07dynamic21foreignMethodDispatchyyF
func foreignMethodDispatch() {
  // CHECK: function_ref @_T0So9GuisemeauC{{[_0-9a-zA-Z]*}}fC
  let g = Guisemeau()!
  // CHECK: class_method [volatile] {{%.*}} : $Gizmo, #Gizmo.frob!1.foreign
  g.frob()
  // CHECK: class_method [volatile] {{%.*}} : $Gizmo, #Gizmo.count!getter.1.foreign
  let x = g.count
  // CHECK: class_method [volatile] {{%.*}} : $Gizmo, #Gizmo.count!setter.1.foreign
  g.count = x
  // CHECK: class_method [volatile] {{%.*}} : $Guisemeau, #Guisemeau.subscript!getter.1.foreign
  let y: Any! = g[0]
  // CHECK: class_method [volatile] {{%.*}} : $Guisemeau, #Guisemeau.subscript!setter.1.foreign
  g[0] = y
  // CHECK: class_method [volatile] {{%.*}} : $NSObject, #NSObject.description!getter.1.foreign
  _ = g.description
}

extension Gizmo {
  // CHECK-LABEL: sil hidden @_T0So5GizmoC7dynamicE{{[_0-9a-zA-Z]*}}fc
  // CHECK:         class_method [volatile] {{%.*}} : $Gizmo, #Gizmo.init!initializer.1.foreign
  convenience init(convenienceInExtension: Int) {
    self.init(bellsOn: convenienceInExtension)
  }

  // CHECK-LABEL: sil hidden @_T0So5GizmoC7dynamicE{{[_0-9a-zA-Z]*}}fC
  // CHECK:         class_method [volatile] {{%.*}} : $@thick Gizmo.Type, #Gizmo.init!allocator.1.foreign
  convenience init(foreignClassFactory x: Int) {
    self.init(stuff: x)
  }

  // CHECK-LABEL: sil hidden @_T0So5GizmoC7dynamicE{{[_0-9a-zA-Z]*}}fC
  // CHECK:         class_method [volatile] {{%.*}} : $@thick Gizmo.Type, #Gizmo.init!allocator.1.foreign
  convenience init(foreignClassExactFactory x: Int) {
    self.init(exactlyStuff: x)
  }

  @objc func foreignObjCExtension() { }
  dynamic func foreignDynamicExtension() { }
}

// CHECK-LABEL: sil hidden @_T07dynamic24foreignExtensionDispatchySo5GizmoCF
// CHECK: bb0([[ARG:%.*]] : $Gizmo):
func foreignExtensionDispatch(_ g: Gizmo) {
  // CHECK: [[BORROWED_ARG:%.*]] = begin_borrow [[ARG]]
  // CHECK: class_method [volatile] [[BORROWED_ARG]] : $Gizmo, #Gizmo.foreignObjCExtension!1.foreign : (Gizmo)
  g.foreignObjCExtension()
  // CHECK: [[BORROWED_ARG:%.*]] = begin_borrow [[ARG]]
  // CHECK: class_method [volatile] [[BORROWED_ARG]] : $Gizmo, #Gizmo.foreignDynamicExtension!1.foreign
  g.foreignDynamicExtension()
}


// CHECK-LABEL: sil hidden @_T07dynamic33nativeMethodDispatchFromOtherFileyyF : $@convention(thin) () -> ()
func nativeMethodDispatchFromOtherFile() {
  // CHECK: function_ref @_T07dynamic13FromOtherFile{{[_0-9a-zA-Z]*}}fC
  let c = FromOtherFile(native: 0)
  // CHECK: class_method {{%.*}} : $FromOtherFile, #FromOtherFile.nativeMethod!1 :
  c.nativeMethod()
  // CHECK: class_method {{%.*}} : $FromOtherFile, #FromOtherFile.nativeProp!getter.1 :
  let x = c.nativeProp
  // CHECK: class_method {{%.*}} : $FromOtherFile, #FromOtherFile.nativeProp!setter.1 :
  c.nativeProp = x
  // CHECK: class_method {{%.*}} : $FromOtherFile, #FromOtherFile.subscript!getter.1 :
  let y = c[native: 0]
  // CHECK: class_method {{%.*}} : $FromOtherFile, #FromOtherFile.subscript!setter.1 :
  c[native: 0] = y
}

// CHECK-LABEL: sil hidden @_T07dynamic31objcMethodDispatchFromOtherFileyyF : $@convention(thin) () -> ()
func objcMethodDispatchFromOtherFile() {
  // CHECK: function_ref @_T07dynamic13FromOtherFile{{[_0-9a-zA-Z]*}}fC
  let c = FromOtherFile(objc: 0)
  // CHECK: class_method {{%.*}} : $FromOtherFile, #FromOtherFile.objcMethod!1 :
  c.objcMethod()
  // CHECK: class_method {{%.*}} : $FromOtherFile, #FromOtherFile.objcProp!getter.1 :
  let x = c.objcProp
  // CHECK: class_method {{%.*}} : $FromOtherFile, #FromOtherFile.objcProp!setter.1 :
  c.objcProp = x
  // CHECK: class_method {{%.*}} : $FromOtherFile, #FromOtherFile.subscript!getter.1 :
  let y = c[objc: 0]
  // CHECK: class_method {{%.*}} : $FromOtherFile, #FromOtherFile.subscript!setter.1 :
  c[objc: 0] = y
}

// CHECK-LABEL: sil hidden @_T07dynamic0A27MethodDispatchFromOtherFileyyF : $@convention(thin) () -> ()
func dynamicMethodDispatchFromOtherFile() {
  // CHECK: function_ref @_T07dynamic13FromOtherFile{{[_0-9a-zA-Z]*}}fC
  let c = FromOtherFile(dynamic: 0)
  // CHECK: class_method [volatile] {{%.*}} : $FromOtherFile, #FromOtherFile.dynamicMethod!1.foreign
  c.dynamicMethod()
  // CHECK: class_method [volatile] {{%.*}} : $FromOtherFile, #FromOtherFile.dynamicProp!getter.1.foreign
  let x = c.dynamicProp
  // CHECK: class_method [volatile] {{%.*}} : $FromOtherFile, #FromOtherFile.dynamicProp!setter.1.foreign
  c.dynamicProp = x
  // CHECK: class_method [volatile] {{%.*}} : $FromOtherFile, #FromOtherFile.subscript!getter.1.foreign
  let y = c[dynamic: 0]
  // CHECK: class_method [volatile] {{%.*}} : $FromOtherFile, #FromOtherFile.subscript!setter.1.foreign
  c[dynamic: 0] = y
}

// CHECK-LABEL: sil hidden @_T07dynamic28managedDispatchFromOtherFileyAA0deF0CF
func managedDispatchFromOtherFile(_ c: FromOtherFile) {
  // CHECK: class_method [volatile] {{%.*}} : $FromOtherFile, #FromOtherFile.managedProp!getter.1.foreign
  let x = c.managedProp
  // CHECK: class_method [volatile] {{%.*}} : $FromOtherFile, #FromOtherFile.managedProp!setter.1.foreign
  c.managedProp = x
}

// CHECK-LABEL: sil hidden @_T07dynamic0A16ExtensionMethodsyAA13ObjCOtherFileCF
func dynamicExtensionMethods(_ obj: ObjCOtherFile) {
  // CHECK: class_method [volatile] {{%.*}} : $ObjCOtherFile, #ObjCOtherFile.extensionMethod!1.foreign
  obj.extensionMethod()
  // CHECK: class_method [volatile] {{%.*}} : $ObjCOtherFile, #ObjCOtherFile.extensionProp!getter.1.foreign
  _ = obj.extensionProp

  // CHECK: class_method [volatile] {{%.*}} : $@thick ObjCOtherFile.Type, #ObjCOtherFile.extensionClassProp!getter.1.foreign
  // CHECK-NEXT: thick_to_objc_metatype {{%.*}} : $@thick ObjCOtherFile.Type to $@objc_metatype ObjCOtherFile.Type
  _ = type(of: obj).extensionClassProp

  // CHECK: class_method [volatile] {{%.*}} : $ObjCOtherFile, #ObjCOtherFile.dynExtensionMethod!1.foreign
  obj.dynExtensionMethod()
  // CHECK: class_method [volatile] {{%.*}} : $ObjCOtherFile, #ObjCOtherFile.dynExtensionProp!getter.1.foreign
  _ = obj.dynExtensionProp

  // CHECK: class_method [volatile] {{%.*}} : $@thick ObjCOtherFile.Type, #ObjCOtherFile.dynExtensionClassProp!getter.1.foreign
  // CHECK-NEXT: thick_to_objc_metatype {{%.*}} : $@thick ObjCOtherFile.Type to $@objc_metatype ObjCOtherFile.Type
  _ = type(of: obj).dynExtensionClassProp
}

public class Base {
  dynamic var x: Bool { return false }
}

public class Sub : Base {
  // CHECK-LABEL: sil hidden @_T07dynamic3SubC1xSbfg : $@convention(method) (@guaranteed Sub) -> Bool {
  // CHECK: bb0([[SELF:%.*]] : $Sub):
  // CHECK:     [[AUTOCLOSURE:%.*]] = function_ref @_T07dynamic3SubC1xSbfgSbyKXKfu_ : $@convention(thin) (@owned Sub) -> (Bool, @error Error)
  // CHECK:     [[SELF_COPY:%.*]] = copy_value [[SELF]]
  // CHECK:     = partial_apply [[AUTOCLOSURE]]([[SELF_COPY]])
  // CHECK:     return {{%.*}} : $Bool
  // CHECK: } // end sil function '_T07dynamic3SubC1xSbfg'

  // CHECK-LABEL: sil shared [transparent] @_T07dynamic3SubC1xSbfgSbyKXKfu_ : $@convention(thin) (@owned Sub) -> (Bool, @error Error) {
  // CHECK: bb0([[VALUE:%.*]] : $Sub):
  // CHECK:     [[BORROWED_VALUE:%.*]] = begin_borrow [[VALUE]]
  // CHECK:     [[VALUE_COPY:%.*]] = copy_value [[BORROWED_VALUE]]
  // CHECK:     [[CASTED_VALUE_COPY:%.*]] = upcast [[VALUE_COPY]]
  // CHECK:     [[SUPER:%.*]] = super_method [volatile] [[VALUE_COPY]] : $Sub, #Base.x!getter.1.foreign : (Base) -> () -> Bool, $@convention(objc_method) (Base) -> ObjCBool
  // CHECK:     = apply [[SUPER]]([[CASTED_VALUE_COPY]])
  // CHECK:     destroy_value [[VALUE_COPY]]
  // CHECK:     end_borrow [[BORROWED_VALUE]] from [[VALUE]]
  // CHECK:     destroy_value [[VALUE]]
  // CHECK: } // end sil function '_T07dynamic3SubC1xSbfgSbyKXKfu_'
  override var x: Bool { return false || super.x }
}


// Vtable contains entries for native and @objc methods, but not dynamic ones
// CHECK-LABEL: sil_vtable Foo {
// CHECK-LABEL:   #Foo.init!initializer.1: {{.*}} :   _T07dynamic3FooCACSi6native_tcfc
// CHECK-LABEL:   #Foo.nativeMethod!1: {{.*}} :       _T07dynamic3FooC12nativeMethodyyF
// CHECK-LABEL:   #Foo.subscript!getter.1: {{.*}} :   _T07dynamic3FooC9subscriptSiSi6native_tcfg    // dynamic.Foo.subscript.getter : (native : Swift.Int) -> Swift.Int
// CHECK-LABEL:   #Foo.subscript!setter.1: {{.*}} :   _T07dynamic3FooC9subscriptSiSi6native_tcfs    // dynamic.Foo.subscript.setter : (native : Swift.Int) -> Swift.Int
// CHECK-LABEL:   #Foo.init!initializer.1: {{.*}} :   _T07dynamic3FooCACSi4objc_tcfc
// CHECK-LABEL:   #Foo.objcMethod!1: {{.*}} :         _T07dynamic3FooC10objcMethodyyF
// CHECK-LABEL:   #Foo.subscript!getter.1: {{.*}} : _T07dynamic3FooC9subscriptSis9AnyObject_p4objc_tcfg // dynamic.Foo.subscript.getter : (objc : Swift.AnyObject) -> Swift.Int
// CHECK-LABEL:   #Foo.subscript!setter.1: {{.*}} : _T07dynamic3FooC9subscriptSis9AnyObject_p4objc_tcfs // dynamic.Foo.subscript.setter : (objc : Swift.AnyObject) -> Swift.Int
// CHECK-NOT:     dynamic.Foo.init (dynamic.Foo.Type)(dynamic : Swift.Int) -> dynamic.Foo
// CHECK-NOT:     dynamic.Foo.dynamicMethod
// CHECK-NOT:     dynamic.Foo.subscript.getter (dynamic : Swift.Int) -> Swift.Int
// CHECK-NOT:     dynamic.Foo.subscript.setter (dynamic : Swift.Int) -> Swift.Int
// CHECK-LABEL:   #Foo.overriddenByDynamic!1: {{.*}} : _T07dynamic3FooC19overriddenByDynamic{{[_0-9a-zA-Z]*}}F
// CHECK-LABEL:   #Foo.nativeProp!getter.1: {{.*}} :  _T07dynamic3FooC10nativePropSifg     // dynamic.Foo.nativeProp.getter : Swift.Int
// CHECK-LABEL:   #Foo.nativeProp!setter.1: {{.*}} :  _T07dynamic3FooC10nativePropSifs     // dynamic.Foo.nativeProp.setter : Swift.Int
// CHECK-LABEL:   #Foo.objcProp!getter.1: {{.*}} :    _T07dynamic3FooC8objcPropSifg  // dynamic.Foo.objcProp.getter : Swift.Int
// CHECK-LABEL:   #Foo.objcProp!setter.1: {{.*}} :    _T07dynamic3FooC8objcPropSifs  // dynamic.Foo.objcProp.setter : Swift.Int
// CHECK-NOT:     dynamic.Foo.dynamicProp.getter
// CHECK-NOT:     dynamic.Foo.dynamicProp.setter

// Vtable uses a dynamic thunk for dynamic overrides
// CHECK-LABEL: sil_vtable Subclass {
// CHECK-LABEL:   #Foo.overriddenByDynamic!1: {{.*}} : public _T07dynamic8SubclassC19overriddenByDynamic{{[_0-9a-zA-Z]*}}FTD

