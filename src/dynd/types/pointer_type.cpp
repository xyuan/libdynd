//
// Copyright (C) 2011-15 DyND Developers
// BSD 2-Clause License, see LICENSE.txt
//

#include <dynd/array.hpp>
#include <dynd/callable.hpp>
#include <dynd/types/pointer_type.hpp>
#include <dynd/memblock/pod_memory_block.hpp>
#include <dynd/kernels/assignment_kernels.hpp>

#include <algorithm>

using namespace std;
using namespace dynd;

ndt::pointer_type::pointer_type(const type &target_tp)
    : base_expr_type(pointer_id, sizeof(void *), alignof(void *),
                     inherited_flags(target_tp.get_flags(), type_flag_zeroinit | type_flag_blockref),
                     sizeof(pointer_type_arrmeta) + target_tp.get_arrmeta_size(), target_tp.get_ndim()),
      m_target_tp(target_tp)
{
}

ndt::pointer_type::~pointer_type() {}

void ndt::pointer_type::print_data(std::ostream &o, const char *arrmeta, const char *data) const
{
  const pointer_type_arrmeta *md = reinterpret_cast<const pointer_type_arrmeta *>(arrmeta);
  const char *target_data = *reinterpret_cast<const char *const *>(data) + md->offset;
  m_target_tp.print_data(o, arrmeta + sizeof(pointer_type_arrmeta), target_data);
}

void ndt::pointer_type::print_type(std::ostream &o) const { o << "pointer[" << m_target_tp << "]"; }

bool ndt::pointer_type::is_expression() const
{
  // Even though the pointer is an instance of an base_expr_type,
  // we'll only call it an expression if the target is.
  return m_target_tp.is_expression();
}

bool ndt::pointer_type::is_unique_data_owner(const char *arrmeta) const
{
  const pointer_type_arrmeta *md = reinterpret_cast<const pointer_type_arrmeta *>(*arrmeta);
  if (md->blockref && (md->blockref->m_use_count != 1 || (md->blockref->m_type != pod_memory_block_type &&
                                                          md->blockref->m_type != fixed_size_pod_memory_block_type))) {
    return false;
  }
  return true;
}

void ndt::pointer_type::transform_child_types(type_transform_fn_t transform_fn, intptr_t arrmeta_offset, void *extra,
                                              type &out_transformed_tp, bool &out_was_transformed) const
{
  type tmp_tp;
  bool was_transformed = false;
  transform_fn(m_target_tp, arrmeta_offset + sizeof(pointer_type_arrmeta), extra, tmp_tp, was_transformed);
  if (was_transformed) {
    out_transformed_tp = make(tmp_tp);
    out_was_transformed = true;
  }
  else {
    out_transformed_tp = type(this, true);
  }
}

ndt::type ndt::pointer_type::get_canonical_type() const
{
  // The canonical version doesn't include the pointer
  return m_target_tp;
}

const ndt::type &ndt::pointer_type::get_operand_type() const
{
  static type vpt = make(make_type<void>());

  if (m_target_tp.get_id() == pointer_id) {
    return m_target_tp;
  }
  else {
    return vpt;
  }
}

ndt::type ndt::pointer_type::apply_linear_index(intptr_t nindices, const irange *indices, size_t current_i,
                                                const type &root_tp, bool leading_dimension) const
{
  if (nindices == 0) {
    return type(this, true);
  }
  else {
    type dt = m_target_tp.apply_linear_index(nindices, indices, current_i, root_tp, leading_dimension);
    if (dt == m_target_tp) {
      return type(this, true);
    }
    else {
      return make(dt);
    }
  }
}

intptr_t ndt::pointer_type::apply_linear_index(intptr_t nindices, const irange *indices, const char *arrmeta,
                                               const type &result_tp, char *out_arrmeta,
                                               const intrusive_ptr<memory_block_data> &embedded_reference,
                                               size_t current_i, const type &root_tp,
                                               bool DYND_UNUSED(leading_dimension), char **DYND_UNUSED(inout_data),
                                               intrusive_ptr<memory_block_data> &DYND_UNUSED(inout_dataref)) const
{
  const pointer_type_arrmeta *md = reinterpret_cast<const pointer_type_arrmeta *>(arrmeta);
  pointer_type_arrmeta *out_md = reinterpret_cast<pointer_type_arrmeta *>(out_arrmeta);
  // If there are no more indices, copy the rest verbatim
  out_md->blockref = md->blockref;
  out_md->offset = md->offset;
  if (!m_target_tp.is_builtin()) {
    intrusive_ptr<memory_block_data> tmp;
    const pointer_type *pdt = result_tp.extended<pointer_type>();
    // The indexing may cause a change to the arrmeta offset
    out_md->offset += m_target_tp.extended()->apply_linear_index(
        nindices, indices, arrmeta + sizeof(pointer_type_arrmeta), pdt->m_target_tp,
        out_arrmeta + sizeof(pointer_type_arrmeta), embedded_reference, current_i, root_tp, false, NULL, tmp);
  }
  return 0;
}

ndt::type ndt::pointer_type::at_single(intptr_t i0, const char **inout_arrmeta, const char **inout_data) const
{
  // If arrmeta/data is provided, follow the pointer and call the target
  // type's at_single
  if (inout_arrmeta) {
    const pointer_type_arrmeta *md = reinterpret_cast<const pointer_type_arrmeta *>(*inout_arrmeta);
    // Modify the arrmeta
    *inout_arrmeta += sizeof(pointer_type_arrmeta);
    // If requested, modify the data pointer
    if (inout_data) {
      *inout_data = *reinterpret_cast<const char *const *>(inout_data) + md->offset;
    }
  }
  // In at_single, we can't maintain a pointer wrapper, this would result in
  // a type inconsistent with its arrmeta.
  return m_target_tp.at_single(i0, inout_arrmeta, inout_data);
}

ndt::type ndt::pointer_type::get_type_at_dimension(char **inout_arrmeta, intptr_t i, intptr_t total_ndim) const
{
  if (i == 0) {
    return type(this, true);
  }
  else {
    if (inout_arrmeta != NULL) {
      *inout_arrmeta += sizeof(pointer_type_arrmeta);
    }
    return make(m_target_tp.get_type_at_dimension(inout_arrmeta, i, total_ndim));
  }
}

void ndt::pointer_type::get_shape(intptr_t ndim, intptr_t i, intptr_t *out_shape, const char *arrmeta,
                                  const char *data) const
{
  if (!m_target_tp.is_builtin()) {
    const char *target_data = NULL;
    if (arrmeta != NULL && data != NULL) {
      const pointer_type_arrmeta *md = reinterpret_cast<const pointer_type_arrmeta *>(arrmeta);
      target_data = *reinterpret_cast<const char *const *>(data) + md->offset;
    }
    m_target_tp.extended()->get_shape(ndim, i, out_shape, arrmeta ? (arrmeta + sizeof(pointer_type_arrmeta)) : NULL,
                                      target_data);
  }
  else {
    stringstream ss;
    ss << "requested too many dimensions from type " << m_target_tp;
    throw runtime_error(ss.str());
  }
}

axis_order_classification_t ndt::pointer_type::classify_axis_order(const char *arrmeta) const
{
  // Return the classification of the target type
  if (m_target_tp.get_ndim() > 1) {
    return m_target_tp.extended()->classify_axis_order(arrmeta + sizeof(pointer_type_arrmeta));
  }
  else {
    return axis_order_none;
  }
}

bool ndt::pointer_type::is_lossless_assignment(const type &dst_tp, const type &src_tp) const
{
  if (dst_tp.extended() == this) {
    return ::is_lossless_assignment(m_target_tp, src_tp);
  }
  else {
    return ::is_lossless_assignment(dst_tp, m_target_tp);
  }
}

bool ndt::pointer_type::operator==(const base_type &rhs) const
{
  if (this == &rhs) {
    return true;
  }
  else if (rhs.get_id() != pointer_id) {
    return false;
  }
  else {
    const pointer_type *dt = static_cast<const pointer_type *>(&rhs);
    return m_target_tp == dt->m_target_tp;
  }
}

ndt::type ndt::pointer_type::with_replaced_storage_type(const type & /*replacement_tp*/) const
{
  throw runtime_error("TODO: implement pointer_type::with_replaced_storage_type");
}

void ndt::pointer_type::arrmeta_default_construct(char *arrmeta, bool blockref_alloc) const
{
  // Simply allocate a POD memory block
  // TODO: Will need a different kind of memory block if the data isn't POD.
  if (blockref_alloc) {
    pointer_type_arrmeta *md = reinterpret_cast<pointer_type_arrmeta *>(arrmeta);
    md->blockref = make_pod_memory_block(m_target_tp).release();
  }
  if (!m_target_tp.is_builtin()) {
    m_target_tp.extended()->arrmeta_default_construct(arrmeta + sizeof(pointer_type_arrmeta), blockref_alloc);
  }
}

void ndt::pointer_type::arrmeta_copy_construct(char *dst_arrmeta, const char *src_arrmeta,
                                               const intrusive_ptr<memory_block_data> &embedded_reference) const
{
  // Copy the blockref, switching it to the embedded_reference if necessary
  const pointer_type_arrmeta *src_md = reinterpret_cast<const pointer_type_arrmeta *>(src_arrmeta);
  pointer_type_arrmeta *dst_md = reinterpret_cast<pointer_type_arrmeta *>(dst_arrmeta);
  dst_md->blockref = src_md->blockref ? src_md->blockref : embedded_reference;
  dst_md->offset = src_md->offset;
  // Copy the target arrmeta
  if (!m_target_tp.is_builtin()) {
    m_target_tp.extended()->arrmeta_copy_construct(dst_arrmeta + sizeof(pointer_type_arrmeta),
                                                   src_arrmeta + sizeof(pointer_type_arrmeta), embedded_reference);
  }
}

void ndt::pointer_type::arrmeta_reset_buffers(char *DYND_UNUSED(arrmeta)) const
{
  throw runtime_error("TODO implement pointer_type::arrmeta_reset_buffers");
}

void ndt::pointer_type::arrmeta_finalize_buffers(char *arrmeta) const
{
  pointer_type_arrmeta *md = reinterpret_cast<pointer_type_arrmeta *>(arrmeta);
  if (md->blockref) {
    // Finalize the memory block
    md->blockref->finalize();
  }
}

void ndt::pointer_type::arrmeta_destruct(char *arrmeta) const
{
  pointer_type_arrmeta *md = reinterpret_cast<pointer_type_arrmeta *>(arrmeta);
  md->~pointer_type_arrmeta();
  if (!m_target_tp.is_builtin()) {
    m_target_tp.extended()->arrmeta_destruct(arrmeta + sizeof(pointer_type_arrmeta));
  }
}

void ndt::pointer_type::arrmeta_debug_print(const char *arrmeta, std::ostream &o, const std::string &indent) const
{
  const pointer_type_arrmeta *md = reinterpret_cast<const pointer_type_arrmeta *>(arrmeta);
  o << indent << "pointer arrmeta\n";
  o << indent << " offset: " << md->offset << "\n";
  memory_block_debug_print(md->blockref.get(), o, indent + " ");
  if (!m_target_tp.is_builtin()) {
    m_target_tp.extended()->arrmeta_debug_print(arrmeta + sizeof(pointer_type_arrmeta), o, indent + " ");
  }
}

bool ndt::pointer_type::match(const type &candidate_tp, std::map<std::string, type> &tp_vars) const
{
  if (candidate_tp.get_id() != pointer_id) {
    return false;
  }

  return m_target_tp.match(candidate_tp.extended<pointer_type>()->m_target_tp, tp_vars);
}

static ndt::type property_get_target_type(ndt::type tp)
{
  const ndt::pointer_type *pd = tp.extended<ndt::pointer_type>();
  return pd->get_target_type();
}

std::map<std::string, nd::callable> ndt::pointer_type::get_dynamic_type_properties() const
{
  std::map<std::string, nd::callable> properties;
  properties["target_type"] = nd::functional::apply(&property_get_target_type, "self");

  return properties;
}

ndt::type ndt::pointer_type::make(const type &target_tp) { return type(new pointer_type(target_tp), false); }
