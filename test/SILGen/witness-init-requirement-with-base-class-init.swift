// RUN: %target-swift-frontend -emit-silgen %s | %FileCheck %s
// RUN: %target-swift-frontend -emit-sil -verify %s

protocol BestFriend: class {
  init()
  static func create() -> Self
}

class Animal {
  required init(species: String) {}

  static func create() -> Self { return self.init() }
  required convenience init() { self.init(species: "\(type(of: self))") }
}

class Dog: Animal, BestFriend {}
// CHECK-LABEL: sil hidden [transparent] [thunk] @_TTWC4main3DogS_10BestFriendS_FS1_CfT_x
// CHECK:         [[SELF:%.*]] = apply
// CHECK:         unchecked_ref_cast [[SELF]] : $Animal to $Dog
// CHECK-LABEL: sil hidden [transparent] [thunk] @_TTWC4main3DogS_10BestFriendS_ZFS1_6createfT_x
// CHECK:         [[SELF:%.*]] = apply
// CHECK:         unchecked_ref_cast [[SELF]] : $Animal to $Dog
