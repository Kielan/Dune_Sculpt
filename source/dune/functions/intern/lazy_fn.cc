#include "lib_array.hh"

#include "fn_lazy.hh"

namespace dune::fn::lazy_fn {

std::string LazyFn::name() const
{
  return debug_name_;
}

std::string LazyFn::input_name(int index) const
{
  return inputs_[index].debug_name;
}

std::string LazyFn::output_name(int index) const
{
  return outputs_[index].debug_name;
}

void *LazyFn::init_storage(LinearAllocator<> & /*allocator*/) const
{
  return nullptr;
}

void LazyFn::destruct_storage(void *storage) const
{
  lib_assert(storage == nullptr);
  UNUSED_VARS_NDEBUG(storage);
}

void LazyFn::possible_output_dependencies(const int /*output_index*/,
                                                const FunctionRef<void(Span<int>)> fn) const
{
  /* The output depends on all inputs by default. */
  Vector<int, 16> indices(inputs_.size());
  for (const int i : inputs_.index_range()) {
    indices[i] = i;
  }
  fn(indices);
}

bool LazyFn::always_used_inputs_available(const Params &params) const
{
  if (allow_missing_requested_inputs_) {
    return true;
  }
  for (const int i : inputs_.index_range()) {
    const Input &fn_input = inputs_[i];
    if (fn_input.usage == ValueUsage::Used) {
      if (params.try_get_input_data_ptr(i) == nullptr) {
        return false;
      }
    }
  }
  return true;
}

bool Params::try_enable_multi_threading_impl()
{
  return false;
}

destruct_ptr<LocalUserData> UserData::get_local(LinearAllocator<> & /*allocator*/)
{
  return {};
}

}  // namespace blender::fn::lazy_function
