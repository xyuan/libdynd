//
// Copyright (C) 2011-14 DyND Developers
// BSD 2-Clause License, see LICENSE.txt
//

#pragma once

#include <dynd/func/arrfunc.hpp>
#include <dynd/kernels/apply_kernels.hpp>

namespace dynd {
namespace nd {
  namespace apply {
    /**
     * Makes an arrfunc out of function ``func``, using the provided keyword
     * parameter names. This function takes ``func`` as a template
     * parameter, so can call it efficiently.
     */
    template <kernel_request_t kernreq, typename func_type, func_type func,
              typename... T>
    arrfunc make(T &&... names)
    {
      typedef kernels::apply_function_ck<kernreq, func_type, func,
                                         arity_of<func_type>::value -
                                             sizeof...(T)> ck_type;

      arrfunc_type_data self(&ck_type::instantiate, NULL, NULL, NULL);
      ndt::type self_tp =
          ndt::make_arrfunc<typename funcproto_of<func_type>::type>(
              std::forward<T>(names)...);

      return arrfunc(&self, self_tp);
    }

    template <typename func_type, func_type func, typename... T>
    arrfunc make(T &&... names)
    {
      return make<kernel_request_host, func_type, func>(
          std::forward<T>(names)...);
    }

    /**
     * Makes an arrfunc out of the function object ``func``, using the provided
     * keyword parameter names. This version makes a copy of provided ``func``
     * object.
     */
    template <kernel_request_t kernreq, typename func_type, typename... T>
    typename std::enable_if<!is_function_pointer<func_type>::value,
                            arrfunc>::type
    make(const func_type &func, T &&... names)
    {
      typedef kernels::apply_callable_ck<kernreq, func_type,
                                         arity_of<func_type>::value -
                                             sizeof...(T)> ck_type;

      ndt::type self_tp =
          ndt::make_arrfunc<kernreq, typename funcproto_of<func_type>::type>(
              std::forward<T>(names)...);

      return arrfunc(func, &ck_type::instantiate, NULL, NULL, NULL, self_tp);
    }

    template <typename func_type, typename... T>
    arrfunc make(const func_type &func, T &&... names)
    {
      return make<kernel_request_host>(func, std::forward<T>(names)...);
    }

    template <kernel_request_t kernreq, typename func_type, typename... T>
    arrfunc make(func_type *func, arrfunc_free_t free, T &&... names)
    {
      typedef kernels::apply_callable_ck<kernreq, func_type *,
                                         arity_of<func_type>::value -
                                             sizeof...(T)> ck_type;

      ndt::type self_tp =
          ndt::make_arrfunc<kernreq, typename funcproto_of<func_type>::type>(
              std::forward<T>(names)...);

      return arrfunc(func, &ck_type::instantiate, NULL, NULL, free, self_tp);
    }

    template <kernel_request_t kernreq, typename func_type, typename... T>
    arrfunc make(func_type *func, T &&... names)
    {
      return make<kernreq>(func, static_cast<arrfunc_free_t>(NULL),
                           std::forward<T>(names)...);
    }

    template <typename func_type, typename... T>
    arrfunc make(func_type *func, arrfunc_free_t free, T &&... names)
    {
      return make<kernel_request_host>(func, free, std::forward<T>(names)...);
    }

    template <typename func_type, typename... T>
    arrfunc make(func_type *func, T &&... names)
    {
      return make<kernel_request_host>(func, std::forward<T>(names)...);
    }

    /**
     * Makes an arrfunc out of the provided function object type, specialized
     * for a memory_type such as cuda_device based on the ``kernreq``.
     */
    template <kernel_request_t kernreq, typename func_type, typename... K,
              typename... T>
    arrfunc make(T &&... names)
    {
      typedef kernels::construct_then_apply_callable_ck<kernreq, func_type,
                                                        K...> ck_type;

      arrfunc_type_data self(&ck_type::instantiate, NULL, NULL, NULL);
      ndt::type self_tp = ndt::make_arrfunc<
          kernreq, typename funcproto_of<func_type, K...>::type>(
          std::forward<T>(names)...);

      return arrfunc(&self, self_tp);
    }

    /**
     * Makes an arrfunc out of the provided function object type, which
     * constructs and calls the function object on demand.
     */
    template <typename func_type, typename... K, typename... T>
    arrfunc make(T &&... names)
    {
      return make<kernel_request_host, func_type, K...>(
          std::forward<T>(names)...);
    }
  } // namespace apply
} // namespace nd
} // namespace dynd