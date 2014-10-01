//
// Copyright (C) 2011-14 Irwin Zaid, DyND Developers
// BSD 2-Clause License, see LICENSE.txt
//

#include <dynd/types/typevar_dim_pow_type.hpp>
#include <dynd/types/typevar_type.hpp>
#include <dynd/types/fixed_dim_type.hpp>
#include <dynd/types/typevar_dim_type.hpp>
#include <dynd/func/make_callable.hpp>

using namespace std;
using namespace dynd;

typevar_dim_pow_type::typevar_dim_pow_type(const ndt::type &base_tp, const nd::string &pow,
                                   const ndt::type &element_type)
    : base_dim_type(typevar_dim_pow_type_id, element_type, 0, 1, 0,
                            type_flag_symbolic, false),
      m_base_tp(base_tp), m_pow(pow)
{
/*
    if (m_name.is_null()) {
        throw type_error("dynd typevar name cannot be null");
    } else if(!is_valid_typevar_name(m_name.begin(), m_name.end())) {
        stringstream ss;
        ss << "dynd typevar name \"";
        print_escaped_utf8_string(ss, m_name.begin(), m_name.end());
        ss << "\" is not valid, it must be alphanumeric and begin with a capital";
        throw type_error(ss.str());
    }
*/
}

void typevar_dim_pow_type::print_data(std::ostream &DYND_UNUSED(o),
                                const char *DYND_UNUSED(arrmeta),
                                const char *DYND_UNUSED(data)) const
{
    throw type_error("Cannot store data of typevar type");
}

void typevar_dim_pow_type::print_type(std::ostream& o) const
{
    switch (m_base_tp.get_type_id()) {
        case fixed_dim_type_id:
            o << m_base_tp.tcast<fixed_dim_type>()->get_fixed_dim_size();
            break;
        case cfixed_dim_type_id:
            o << m_base_tp.tcast<cfixed_dim_type>()->get_fixed_dim_size();
            break;
        case strided_dim_type_id:
            o << "strided";
            break;
        case var_dim_type_id:
            o << "var";
            break;
        case typevar_dim_type_id:
            o << m_base_tp.tcast<typevar_dim_type>()->get_name_str();
            break;
        default:
            break;
    }

    o << "**" << m_pow.str() << " * " << get_element_type();
}

ndt::type typevar_dim_pow_type::apply_linear_index(
    intptr_t DYND_UNUSED(nindices), const irange *DYND_UNUSED(indices),
    size_t DYND_UNUSED(current_i), const ndt::type &DYND_UNUSED(root_tp),
    bool DYND_UNUSED(leading_dimension)) const
{
    throw type_error("Cannot store data of typevar type");
}

intptr_t typevar_dim_pow_type::apply_linear_index(
    intptr_t DYND_UNUSED(nindices), const irange *DYND_UNUSED(indices),
    const char *DYND_UNUSED(arrmeta), const ndt::type &DYND_UNUSED(result_tp),
    char *DYND_UNUSED(out_arrmeta), memory_block_data *DYND_UNUSED(embedded_reference), size_t DYND_UNUSED(current_i),
    const ndt::type &DYND_UNUSED(root_tp), bool DYND_UNUSED(leading_dimension), char **DYND_UNUSED(inout_data),
    memory_block_data **DYND_UNUSED(inout_dataref)) const
{
    throw type_error("Cannot store data of typevar type");
}

intptr_t typevar_dim_pow_type::get_dim_size(const char *DYND_UNUSED(arrmeta),
                                        const char *DYND_UNUSED(data)) const
{
    return -1;
}

bool typevar_dim_pow_type::is_lossless_assignment(const ndt::type& DYND_UNUSED(dst_tp), const ndt::type& DYND_UNUSED(src_tp)) const
{
/*
    if (dst_tp.extended() == this) {
        if (src_tp.extended() == this) {
            return true;
        } else if (src_tp.get_type_id() == typevar_dim_type_id) {
            return *dst_tp.extended() == *src_tp.extended();
        }
    }

    return false;
*/
    return false;
}

bool typevar_dim_pow_type::operator==(const base_type& DYND_UNUSED(rhs)) const
{
    return false;
/*
    if (this == &rhs) {
        return true;
    } else if (rhs.get_type_id() != typevar_dim_type_id) {
        return false;
    } else {
        const typevar_dim_type *tvt =
            static_cast<const typevar_dim_type *>(&rhs);
        return m_name == tvt->m_name &&
               m_element_tp == tvt->m_element_tp;
    }
*/
}

void typevar_dim_pow_type::arrmeta_default_construct(
    char *DYND_UNUSED(arrmeta), intptr_t DYND_UNUSED(ndim),
    const intptr_t *DYND_UNUSED(shape), bool DYND_UNUSED(blockref_alloc)) const
{
  throw type_error("Cannot store data of typevar type");
}

void typevar_dim_pow_type::arrmeta_copy_construct(
    char *DYND_UNUSED(dst_arrmeta), const char *DYND_UNUSED(src_arrmeta),
    memory_block_data *DYND_UNUSED(embedded_reference)) const
{
    throw type_error("Cannot store data of typevar type");
}

size_t typevar_dim_pow_type::arrmeta_copy_construct_onedim(
    char *DYND_UNUSED(dst_arrmeta), const char *DYND_UNUSED(src_arrmeta),
    memory_block_data *DYND_UNUSED(embedded_reference)) const
{
    throw type_error("Cannot store data of typevar type");
}

void typevar_dim_pow_type::arrmeta_destruct(char *DYND_UNUSED(arrmeta)) const
{
    throw type_error("Cannot store data of typevar type");
}

/*
static nd::array property_get_name(const ndt::type& tp) {
    return tp.tcast<typevar_dim_type>()->get_name();
}

static ndt::type property_get_element_type(const ndt::type& dt) {
    return dt.tcast<typevar_dim_type>()->get_element_type();
}

void typevar_dim_type::get_dynamic_type_properties(
    const std::pair<std::string, gfunc::callable> **out_properties,
    size_t *out_count) const
{
    static pair<string, gfunc::callable> type_properties[] = {
        pair<string, gfunc::callable>(
            "name", gfunc::make_callable(&property_get_name, "self")),
        pair<string, gfunc::callable>(
            "element_type",
            gfunc::make_callable(&property_get_element_type, "self")), };

    *out_properties = type_properties;
    *out_count = sizeof(type_properties) / sizeof(type_properties[0]);
}
*/
