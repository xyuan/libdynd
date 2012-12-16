//
// Copyright (C) 2011-12, Dynamic NDArray Developers
// BSD 2-Clause License, see LICENSE.txt
//

#include <time.h>

#include <algorithm>

#include <dynd/dtypes/date_dtype.hpp>
#include <dynd/dtypes/date_property_dtype.hpp>
#include <dynd/dtypes/fixedstruct_dtype.hpp>
#include <dynd/dtypes/string_dtype.hpp>
#include <dynd/kernels/single_compare_kernel_instance.hpp>
#include <dynd/kernels/string_assignment_kernels.hpp>
#include <dynd/kernels/assignment_kernels.hpp>
#include <dynd/exceptions.hpp>
#include <dynd/gfunc/make_callable.hpp>
#include <dynd/kernels/date_assignment_kernels.hpp>
#include <dynd/ndobject_iter.hpp>

#include <datetime_strings.h>
#include <datetime_localtime.h>

using namespace std;
using namespace dynd;

inline datetime::datetime_unit_t datetime_unit_from_date_unit(date_unit_t unit)
{
    return unit == date_unit_day ? datetime::datetime_unit_day :
                                   unit == date_unit_month ? datetime::datetime_unit_month :
                                   datetime::datetime_unit_year;
}

date_dtype::date_dtype(date_unit_t unit)
    : extended_dtype(date_type_id, datetime_kind, 4, 4), m_unit(unit)
{
    switch (unit) {
        case date_unit_year:
        case date_unit_month:
        case date_unit_day:
            break;
        default:
            throw runtime_error("Unrecognized date unit in date dtype constructor");
    }
}

date_dtype::~date_dtype()
{
}

void date_dtype::set_ymd(const char *DYND_UNUSED(metadata), char *data,
                assign_error_mode errmode, int32_t year, int32_t month, int32_t day) const
{
    if (errmode != assign_error_none && !datetime::is_valid_ymd(year, month, day)) {
        stringstream ss;
        ss << "invalid input year/month/day " << year << "/" << month << "/" << day;
        throw runtime_error(ss.str());
    }

    switch (m_unit) {
        case date_unit_year:
            if (errmode != assign_error_none && (month != 1 || day != 1)) {
                stringstream ss;
                ss << "out of range year/month/day " << year << "/" << month << "/" << day << " for years unit";
                throw runtime_error(ss.str());
            }
            *reinterpret_cast<int32_t *>(data) = year - 1970;
            break;
        case date_unit_month:
            if (errmode != assign_error_none && (day != 1)) {
                stringstream ss;
                ss << "out of range year/month/day " << year << "/" << month << "/" << day << " for months unit";
                throw runtime_error(ss.str());
            }
            *reinterpret_cast<int32_t *>(data) = 12 * (year - 1970) + (month - 1);
            break;
        case date_unit_day:
            *reinterpret_cast<int32_t *>(data) = datetime::ymd_to_days(year, month, day);
            break;
        default:
            throw runtime_error("date dtype has corrupted date unit");
    }
}

void date_dtype::get_ymd(const char *metadata, const char *data,
                int32_t &out_year, int32_t &out_month, int32_t &out_day) const
{
    datetime::date_ymd ymd;
    datetime::date_to_ymd(*reinterpret_cast<const int32_t *>(data),
                    datetime_unit_from_date_unit(m_unit), ymd);
    out_year = ymd.year;
    out_month = ymd.month;
    out_day = ymd.day;
}

void date_dtype::print_data(std::ostream& o, const char *DYND_UNUSED(metadata), const char *data) const
{
    int32_t value = *reinterpret_cast<const int32_t *>(data);
    switch (m_unit) {
        case date_unit_year:
            o << datetime::make_iso_8601_date(value, datetime::datetime_unit_year);
            break;
        case date_unit_month:
            o << datetime::make_iso_8601_date(value, datetime::datetime_unit_month);
            break;
        case date_unit_day:
            o << datetime::make_iso_8601_date(value, datetime::datetime_unit_day);
            break;
        default:
            o << "<corrupt date dtype>";
            break;
    }
}

void date_dtype::print_dtype(std::ostream& o) const
{
    if (m_unit == date_unit_day) {
        o << "date";
    } else {
        o << "date<" << m_unit << ">";
    }
}

bool date_dtype::is_lossless_assignment(const dtype& dst_dt, const dtype& src_dt) const
{
    if (dst_dt.extended() == this) {
        if (src_dt.extended() == this) {
            return true;
        } else if (src_dt.get_type_id() == date_type_id) {
            const date_dtype *src_fs = static_cast<const date_dtype*>(src_dt.extended());
            return src_fs->m_unit == m_unit;
        } else {
            return false;
        }
    } else {
        return false;
    }
}

void date_dtype::get_single_compare_kernel(single_compare_kernel_instance& /*out_kernel*/) const {
    throw runtime_error("get_single_compare_kernel for date are not implemented");
}

void date_dtype::get_dtype_assignment_kernel(const dtype& dst_dt, const dtype& src_dt,
                assign_error_mode errmode,
                kernel_instance<unary_operation_pair_t>& out_kernel) const
{
    if (this == dst_dt.extended()) {
        if (src_dt.get_kind() == string_kind) {
            // Assignment from strings
            get_string_to_date_assignment_kernel(m_unit, src_dt, errmode, out_kernel);
            return;
        } else if (src_dt.get_kind() == struct_kind) {
            get_struct_to_date_assignment_kernel(m_unit, src_dt, errmode, out_kernel);
            return;
        } else if (!src_dt.is_builtin()) {
            src_dt.extended()->get_dtype_assignment_kernel(dst_dt, src_dt, errmode, out_kernel);
        }
        // TODO
    } else {
        if (dst_dt.get_kind() == string_kind) {
            // Assignment to strings
            get_date_to_string_assignment_kernel(dst_dt, m_unit, errmode, out_kernel);
            return;
        } else if (dst_dt.get_kind() == struct_kind) {
            get_date_to_struct_assignment_kernel(dst_dt, m_unit, errmode, out_kernel);
            return;
        }
        // TODO
    }

    stringstream ss;
    ss << "assignment from " << src_dt << " to " << dst_dt << " is not implemented yet";
    throw runtime_error(ss.str());
}


bool date_dtype::operator==(const extended_dtype& rhs) const
{
    if (this == &rhs) {
        return true;
    } else if (rhs.get_type_id() != date_type_id) {
        return false;
    } else {
        const date_dtype *dt = static_cast<const date_dtype*>(&rhs);
        return m_unit == dt->m_unit;
    }
}

///////// properties on the dtype

static string property_get_unit(const dtype& dt) {
    const date_dtype *d = static_cast<const date_dtype *>(dt.extended());
    stringstream ss;
    ss << d->get_unit();
    return ss.str();
}

static pair<string, gfunc::callable> date_dtype_properties[] = {
    pair<string, gfunc::callable>("unit", gfunc::make_callable(&property_get_unit, "self"))
};

void date_dtype::get_dynamic_dtype_properties(const std::pair<std::string, gfunc::callable> **out_properties, int *out_count) const
{
    *out_properties = date_dtype_properties;
    *out_count = sizeof(date_dtype_properties) / sizeof(date_dtype_properties[0]);
}

///////// functions on the ndobject

static ndobject function_dtype_today(const dtype& dt) {
    datetime::date_ymd ymd;
    datetime::fill_current_local_date(&ymd);
    const date_dtype *d = static_cast<const date_dtype *>(dt.extended());
    ndobject result(dt);
    switch (d->get_unit()) {
        case date_unit_year:
            *reinterpret_cast<int32_t *>(result.get_readwrite_originptr()) = ymd.year - 1970;
            break;
        case date_unit_month:
            *reinterpret_cast<int32_t *>(result.get_readwrite_originptr()) = 12 * (ymd.year - 1970) + (ymd.month - 1);
            break;
        case date_unit_day:
            *reinterpret_cast<int32_t *>(result.get_readwrite_originptr()) = datetime::ymd_to_days(ymd);
            break;
        default:
            throw runtime_error("date dtype has corrupted date unit");
    }
    // Make the result immutable (we own the only reference to the data at this point)
    result.get_ndo()->m_flags = (result.get_ndo()->m_flags&~(uint64_t)write_access_flag)|immutable_access_flag;
    return result;
}

static pair<string, gfunc::callable> date_dtype_functions[] = {
    pair<string, gfunc::callable>("today", gfunc::make_callable(&function_dtype_today, "self"))
};

void date_dtype::get_dynamic_dtype_functions(const std::pair<std::string, gfunc::callable> **out_functions, int *out_count) const
{
    *out_functions = date_dtype_functions;
    *out_count = sizeof(date_dtype_functions) / sizeof(date_dtype_functions[0]);
}

///////// properties on the ndobject

static ndobject property_ndo_get_year(const ndobject& n) {
    return n.view_scalars(make_date_property_dtype(n.get_udtype(), "year"));
}

static ndobject property_ndo_get_month(const ndobject& n) {
    return n.view_scalars(make_date_property_dtype(n.get_udtype(), "month"));
}

static ndobject property_ndo_get_day(const ndobject& n) {
    return n.view_scalars(make_date_property_dtype(n.get_udtype(), "day"));
}

static pair<string, gfunc::callable> date_ndobject_properties[] = {
    pair<string, gfunc::callable>("year", gfunc::make_callable(&property_ndo_get_year, "self")),
    pair<string, gfunc::callable>("month", gfunc::make_callable(&property_ndo_get_month, "self")),
    pair<string, gfunc::callable>("day", gfunc::make_callable(&property_ndo_get_day, "self"))
};

void date_dtype::get_dynamic_ndobject_properties(const std::pair<std::string, gfunc::callable> **out_properties, int *out_count) const
{
    *out_properties = date_ndobject_properties;
    *out_count = sizeof(date_ndobject_properties) / sizeof(date_ndobject_properties[0]);
    // Exclude the day and/or month properties for the larger units
    if (m_unit == date_unit_month) {
        *out_count -= 1;
    } else if (m_unit == date_unit_year) {
        *out_count -= 2;
    }
}

///////// functions on the ndobject

static ndobject function_ndo_to_struct(const ndobject& n) {
    dtype array_dt = n.get_dtype();
    dtype dt = array_dt.get_dtype_at_dimension(NULL, array_dt.get_undim()).value_dtype();
    const date_dtype *dd = static_cast<const date_dtype *>(dt.extended());
    return n.cast_scalars(date_dtype_default_struct_dtypes[dd->get_unit()]);
}

static ndobject function_ndo_strftime(const ndobject& n, const std::string& format) {
    if (format.empty()) {
        throw runtime_error("format string for strftime should not be empty");
    }
    // TODO: lazy evaluation?
    ndobject result = empty_like(n, make_string_dtype(string_encoding_utf_8));
    ndobject_iter<1, 1> iter(result, n);
    kernel_instance<unary_operation_pair_t> kernel;
    unary_kernel_static_data extra;
    if (iter.get_uniform_dtype<1>().get_kind() == expression_kind) {
        get_dtype_assignment_kernel(iter.get_uniform_dtype<1>().value_dtype(), iter.get_uniform_dtype<1>(),
                        assign_error_none, NULL, kernel);
        extra.auxdata = kernel.auxdata;
        extra.dst_metadata = NULL;
        extra.src_metadata = iter.metadata<1>();
    }
    int32_t date;
    const date_dtype *dd = static_cast<const date_dtype *>(iter.get_uniform_dtype<1>().value_dtype().extended());
    const extended_string_dtype *esd = static_cast<const extended_string_dtype *>(iter.get_uniform_dtype<0>().extended());
    datetime::datetime_unit_t datetime_unit = datetime_unit_from_date_unit(dd->get_unit());
    struct tm tm_val;
    string str;
    if (!iter.empty()) {
        do {
            // Get the date
            if (kernel.kernel.single) {
                kernel.kernel.single(reinterpret_cast<char *>(&date), iter.data<1>(), &extra);
            } else {
                date = *reinterpret_cast<const int32_t *>(iter.data<1>());
            }
            // Convert the date to a 'struct tm'
            datetime::date_to_struct_tm(date, datetime_unit, tm_val);
            // Call strftime, growing the string buffer if needed so it fits
            str.resize(format.size() + 16);
#ifdef _MSC_VER
            // Given an invalid format string strftime will abort unless an invalid
            // parameter handler is installed.
            disable_invalid_parameter_handler raii;
#endif
            for(int i = 0; i < 3; ++i) {
                // Force errno to zero
                errno = 0;
                size_t len = strftime(&str[0], str.size(), format.c_str(), &tm_val);
                if (len > 0) {
                    str.resize(len);
                    break;
                } else {
                    if (errno != 0) {
                        stringstream ss;
                        ss << "error in strftime with format string \"" << format << "\" to strftime";
                        throw runtime_error(ss.str());
                    }
                    str.resize(str.size() * 2);
                }
            }
            // Copy the string to the output
            esd->set_utf8_string(iter.metadata<0>(), iter.data<0>(), assign_error_none, str);
        } while(iter.next());
    }
    return result;
}

static ndobject function_ndo_weekday(const ndobject& n) {
    dtype array_dt = n.get_dtype();
    dtype dt = array_dt.get_dtype_at_dimension(NULL, array_dt.get_undim());
    return n.view_scalars(make_date_property_dtype(dt, "weekday"));
}

static ndobject function_ndo_replace(const ndobject& n, int32_t year, int32_t month, int32_t day) {
    // TODO: lazy evaluation?
    if (year == numeric_limits<int32_t>::max() && month == numeric_limits<int32_t>::max() &&
                    day == numeric_limits<int32_t>::max()) {
        throw std::runtime_error("no parameters provided to date.replace, should provide at least one");
    }
    // Create the result array
    ndobject result = empty_like(n);
    ndobject_iter<1, 1> iter(result, n);

    // Get a kernel to produce elements if the input is an expression
    kernel_instance<unary_operation_pair_t> kernel;
    unary_kernel_static_data extra;
    if (iter.get_uniform_dtype<1>().get_kind() == expression_kind) {
        get_dtype_assignment_kernel(iter.get_uniform_dtype<1>().value_dtype(), iter.get_uniform_dtype<1>(),
                        assign_error_none, NULL, kernel);
        extra.auxdata = kernel.auxdata;
        extra.dst_metadata = NULL;
        extra.src_metadata = iter.metadata<1>();
    }
    int32_t date;
    const date_dtype *dd = static_cast<const date_dtype *>(iter.get_uniform_dtype<1>().value_dtype().extended());
    datetime::datetime_unit_t datetime_unit = datetime_unit_from_date_unit(dd->get_unit());
    datetime::date_ymd ymd;
    if (!iter.empty()) {
        // Loop over all the elements
        do {
            // Get the date
            if (kernel.kernel.single) {
                kernel.kernel.single(reinterpret_cast<char *>(&date), iter.data<1>(), &extra);
            } else {
                date = *reinterpret_cast<const int32_t *>(iter.data<1>());
            }
            // Convert the date to a 'struct tm'
            datetime::date_to_ymd(date, datetime_unit, ymd);
            // Replace the values as requested
            if (year != numeric_limits<int32_t>::max()) {
                ymd.year = year;
            }
            if (month != numeric_limits<int32_t>::max()) {
                ymd.month = month;
                if (-12 <= month && month <= -1) {
                    // Use negative months to count from the end (like Python slicing, though
                    // the standard Python datetime.date doesn't support this)
                    ymd.month = month + 13;
                } else if (1 <= month && month <= 12) {
                    ymd.month = month;
                } else {
                    stringstream ss;
                    ss << "invalid month value " << month;
                    throw runtime_error(ss.str());
                }
                // If the day isn't also being replaced, make sure the resulting date is valid
                if (day == numeric_limits<int32_t>::max()) {
                    if (!datetime::is_valid_ymd(ymd)) {
                        stringstream ss;
                        ss << "invalid replace resulting year/month/day " << year << "/" << month << "/" << day;
                        throw runtime_error(ss.str());
                    }
                }
            }
            if (day != numeric_limits<int32_t>::max()) {
                int month_size = datetime::get_month_size(ymd.year, ymd.month);
                if (1 <= day && day <= month_size) {
                    ymd.day = day;
                } else if (-month_size <= day && day <= -1) {
                    // Use negative days to count from the end (like Python slicing, though
                    // the standard Python datetime.date doesn't support this)
                    ymd.day = day + month_size + 1;
                } else {
                    stringstream ss;
                    ss << "invalid day value " << day << " for year/month " << year << "/" << month;
                    throw runtime_error(ss.str());
                }
            }
            dd->set_ymd(iter.metadata<0>(), iter.data<0>(), assign_error_none, ymd.year, ymd.month, ymd.day);
        } while(iter.next());
    }
    return result;
}

static pair<string, gfunc::callable> date_ndobject_functions[] = {
    pair<string, gfunc::callable>("to_struct", gfunc::make_callable(&function_ndo_to_struct, "self")),
    pair<string, gfunc::callable>("strftime", gfunc::make_callable(&function_ndo_strftime, "self", "format")),
    pair<string, gfunc::callable>("weekday", gfunc::make_callable(&function_ndo_weekday, "self")),
    pair<string, gfunc::callable>("replace", gfunc::make_callable_with_default(&function_ndo_replace, "self", "year", "month", "day",
                    numeric_limits<int32_t>::max(), numeric_limits<int32_t>::max(), numeric_limits<int32_t>::max()))
};

void date_dtype::get_dynamic_ndobject_functions(const std::pair<std::string, gfunc::callable> **out_functions, int *out_count) const
{
    *out_functions = date_ndobject_functions;
    *out_count = sizeof(date_ndobject_functions) / sizeof(date_ndobject_functions[0]);
}

///////// property accessor kernels (used by date_property_dtype)

namespace {
    void property_kernel_year_single(char *dst, const char *src, unary_kernel_static_data *extra)
    {
        datetime::datetime_unit_t unit = static_cast<datetime::datetime_unit_t>(get_raw_auxiliary_data(extra->auxdata)>>1);
        datetime::date_ymd fld;
        datetime::date_to_ymd(*reinterpret_cast<const int32_t *>(src), unit, fld);
        *reinterpret_cast<int32_t *>(dst) = fld.year;
    }
    void property_kernel_year_contig(char *dst, const char *src, size_t count, unary_kernel_static_data *extra)
    {
        datetime::datetime_unit_t unit = static_cast<datetime::datetime_unit_t>(get_raw_auxiliary_data(extra->auxdata)>>1);
        datetime::date_ymd fld;
        int32_t *dst_array = reinterpret_cast<int32_t *>(dst);
        const int32_t *src_array = reinterpret_cast<const int32_t *>(src);
        for (size_t i = 0; i != count; ++i, ++src_array, ++dst_array) {
            datetime::date_to_ymd(*src_array, unit, fld);
            *dst_array = fld.year;
        }
    }

    void property_kernel_month_single(char *dst, const char *src, unary_kernel_static_data *extra)
    {
        datetime::datetime_unit_t unit = static_cast<datetime::datetime_unit_t>(get_raw_auxiliary_data(extra->auxdata)>>1);
        datetime::date_ymd fld;
        datetime::date_to_ymd(*reinterpret_cast<const int32_t *>(src), unit, fld);
        *reinterpret_cast<int32_t *>(dst) = fld.month;
    }
    void property_kernel_month_contig(char *dst, const char *src, size_t count, unary_kernel_static_data *extra)
    {
        datetime::datetime_unit_t unit = static_cast<datetime::datetime_unit_t>(get_raw_auxiliary_data(extra->auxdata)>>1);
        datetime::date_ymd fld;
        int32_t *dst_array = reinterpret_cast<int32_t *>(dst);
        const int32_t *src_array = reinterpret_cast<const int32_t *>(src);
        for (size_t i = 0; i != count; ++i, ++src_array, ++dst_array) {
            datetime::date_to_ymd(*src_array, unit, fld);
            *dst_array = fld.month;
        }
    }

    void property_kernel_day_single(char *dst, const char *src, unary_kernel_static_data *extra)
    {
        datetime::datetime_unit_t unit = static_cast<datetime::datetime_unit_t>(get_raw_auxiliary_data(extra->auxdata)>>1);
        datetime::date_ymd fld;
        datetime::date_to_ymd(*reinterpret_cast<const int32_t *>(src), unit, fld);
        *reinterpret_cast<int32_t *>(dst) = fld.day;
    }
    void property_kernel_day_contig(char *dst, const char *src, size_t count, unary_kernel_static_data *extra)
    {
        datetime::datetime_unit_t unit = static_cast<datetime::datetime_unit_t>(get_raw_auxiliary_data(extra->auxdata)>>1);
        datetime::date_ymd fld;
        int32_t *dst_array = reinterpret_cast<int32_t *>(dst);
        const int32_t *src_array = reinterpret_cast<const int32_t *>(src);
        for (size_t i = 0; i != count; ++i, ++src_array, ++dst_array) {
            datetime::date_to_ymd(*src_array, unit, fld);
            *dst_array = fld.day;
        }
    }

    void property_kernel_weekday_single(char *dst, const char *src, unary_kernel_static_data *extra)
    {
        datetime::datetime_unit_t unit = static_cast<datetime::datetime_unit_t>(get_raw_auxiliary_data(extra->auxdata)>>1);
        datetime::date_val_t days;
        datetime::date_to_days(*reinterpret_cast<const int32_t *>(src), unit, days);
        // 1970-01-05 is Monday
        int weekday = (int)((days - 4) % 7);
        if (weekday < 0) {
            weekday += 7;
        }
        *reinterpret_cast<int32_t *>(dst) = weekday;
    }
    void property_kernel_weekday_contig(char *dst, const char *src, size_t count, unary_kernel_static_data *extra)
    {
        datetime::datetime_unit_t unit = static_cast<datetime::datetime_unit_t>(get_raw_auxiliary_data(extra->auxdata)>>1);
        datetime::date_val_t days;
        int32_t *dst_array = reinterpret_cast<int32_t *>(dst);
        const int32_t *src_array = reinterpret_cast<const int32_t *>(src);
        for (size_t i = 0; i != count; ++i, ++src_array, ++dst_array) {
            datetime::date_to_days(*src_array, unit, days);
            // 1970-01-05 is Monday
            int weekday = (int)((days - 4) % 7);
            if (weekday < 0) {
                weekday += 7;
            }
            *dst_array = weekday;
        }
    }
} // anonymous namespace

void date_dtype::get_property_getter_kernel(const std::string& property_name,
                dtype& out_value_dtype, kernel_instance<unary_operation_pair_t>& out_to_value_kernel) const
{
    datetime::datetime_unit_t unit = datetime_unit_from_date_unit(m_unit);

    out_value_dtype = make_dtype<int32_t>();
    make_raw_auxiliary_data(out_to_value_kernel.auxdata, static_cast<uintptr_t>(unit)<<1);
    if (property_name == "year") {
        out_to_value_kernel.kernel.single = &property_kernel_year_single;
        out_to_value_kernel.kernel.contig = &property_kernel_year_contig;
    } else if (m_unit <= date_unit_month && property_name == "month") {
        out_to_value_kernel.kernel.single = &property_kernel_month_single;
        out_to_value_kernel.kernel.contig = &property_kernel_month_contig;
    } else if (m_unit <= date_unit_day && property_name == "day") {
        out_to_value_kernel.kernel.single = &property_kernel_day_single;
        out_to_value_kernel.kernel.contig = &property_kernel_day_contig;
    } else if (m_unit <= date_unit_day && property_name == "weekday") {
        out_to_value_kernel.kernel.single = &property_kernel_weekday_single;
        out_to_value_kernel.kernel.contig = &property_kernel_weekday_contig;
    } else {
        stringstream ss;
        ss << "dynd date dtype does not have a kernel for property " << property_name;
        throw runtime_error(ss.str());
    }
}

