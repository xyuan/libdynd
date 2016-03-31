//
// Copyright (C) 2011-15 DyND Developers
// BSD 2-Clause License, see LICENSE.txt
//

#pragma once

#include <dynd/callables/base_callable.hpp>

namespace dynd {
namespace nd {

  template <typename KernelType>
  class default_instantiable_callable : public base_callable {
  protected:
    using base_callable::base_callable;

  public:
    default_instantiable_callable(const ndt::type &tp) : base_callable(tp) {}

    const ndt::type &resolve(call_graph &cg, const ndt::type &dst_tp, size_t DYND_UNUSED(nsrc),
                             const ndt::type *DYND_UNUSED(src_tp), size_t DYND_UNUSED(nkwd),
                             const array *DYND_UNUSED(kwds),
                             const std::map<std::string, ndt::type> &DYND_UNUSED(tp_vars)) {
      cg.emplace_back(this);
      return dst_tp;
    }

    void new_instantiate(call_frame *DYND_UNUSED(frame), kernel_builder &ckb, kernel_request_t kernreq,
                         const char *DYND_UNUSED(dst_arrmeta), const char *const *DYND_UNUSED(src_arrmeta),
                         size_t DYND_UNUSED(nkwd), const array *DYND_UNUSED(kwds)) {
      ckb.emplace_back<KernelType>(kernreq);
    }

    void instantiate(char *DYND_UNUSED(data), kernel_builder *ckb, const ndt::type &DYND_UNUSED(dst_tp),
                     const char *DYND_UNUSED(dst_arrmeta), intptr_t DYND_UNUSED(nsrc),
                     const ndt::type *DYND_UNUSED(src_tp), const char *const *DYND_UNUSED(src_arrmeta),
                     kernel_request_t kernreq, intptr_t DYND_UNUSED(nkwd), const array *DYND_UNUSED(kwds),
                     const std::map<std::string, ndt::type> &DYND_UNUSED(tp_vars)) {
      ckb->emplace_back<KernelType>(kernreq);
    }
  };

} // namespace dynd::nd
} // namespace dynd