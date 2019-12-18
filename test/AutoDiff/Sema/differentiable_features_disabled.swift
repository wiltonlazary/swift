// RUN: %target-swift-frontend -typecheck -verify %s

// expected-error @+1 {{differentiable programming is an experimental feature that is currently disabled}}
let _: @differentiable (Float) -> Float

func id(_ x: Float) -> Float {
  return x
}
// expected-error @+1 {{differentiable programming is an experimental feature that is currently disabled}}
@derivative(of: id)
func jvpId(x: Float) -> (value: Float, differential: (Float) -> (Float)) {
  return (x, { $0 })
}
