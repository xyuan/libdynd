//
// Copyright (C) 2011-14 Mark Wiebe, DyND Developers
// BSD 2-Clause License, see LICENSE.txt
//

#include <time.h>

#include <cerrno>
#include <algorithm>

#include <dynd/types/datetime_type.hpp>
#include <dynd/types/time_type.hpp>
#include <dynd/types/date_util.hpp>
#include <dynd/types/property_type.hpp>
#include <dynd/types/cstruct_type.hpp>
#include <dynd/types/string_type.hpp>
#include <dynd/types/unary_expr_type.hpp>
#include <dynd/kernels/datetime_assignment_kernels.hpp>
#include <dynd/kernels/date_expr_kernels.hpp>
#include <dynd/kernels/string_assignment_kernels.hpp>
#include <dynd/kernels/assignment_kernels.hpp>
#include <dynd/exceptions.hpp>
#include <dynd/func/make_callable.hpp>
#include <dynd/array_iter.hpp>

#include <datetime_strings.h>
#include <datetime_localtime.h>

using namespace std;
using namespace dynd;

datetime_type::datetime_type(datetime_tz_t timezone)
    : base_type(datetime_type_id, datetime_kind, 8,
                scalar_align_of<int64_t>::value, type_flag_scalar, 0, 0),
      m_timezone(timezone)
{
}

datetime_type::~datetime_type()
{
}

void datetime_type::set_cal(const char *DYND_UNUSED(arrmeta), char *data,
                assign_error_mode errmode,
                int32_t year, int32_t month, int32_t day,
                int32_t hour, int32_t minute, int32_t second, int32_t tick) const
{
    if (errmode != assign_error_none) {
        if (!date_ymd::is_valid(year, month, day)) {
            stringstream ss;
            ss << "invalid input year/month/day " << year << "/" << month << "/" << day;
            throw runtime_error(ss.str());
        }
        if (hour < 0 || hour >= 24) {
            stringstream ss;
            ss << "invalid input hour " << hour << " for " << ndt::type(this, true);
            throw runtime_error(ss.str());
        }
        if (minute < 0 || minute >= 60) {
            stringstream ss;
            ss << "invalid input minute " << minute << " for " << ndt::type(this, true);
            throw runtime_error(ss.str());
        }
        if (second < 0 || second >= 60) {
            stringstream ss;
            ss << "invalid input second " << second << " for " << ndt::type(this, true);
            throw runtime_error(ss.str());
        }
        if (tick < 0 || tick >= 1000000000) {
            stringstream ss;
            ss << "invalid input tick (100*nanosecond) " << tick << " for " << ndt::type(this, true);
            throw runtime_error(ss.str());
        }
    }

    datetime_struct dts;
    dts.ymd.year = year;
    dts.ymd.month = month;
    dts.ymd.day = day;
    dts.hmst.hour = hour;
    dts.hmst.minute = minute;
    dts.hmst.second = second;
    dts.hmst.tick = tick;

    *reinterpret_cast<int64_t *>(data) = dts.to_ticks();
}

void datetime_type::set_utf8_string(const char *DYND_UNUSED(arrmeta),
                                    char *data, const std::string &utf8_str,
                                    const eval::eval_context *ectx) const
{
    // TODO: Use errmode to adjust strictness
    // TODO: Parsing adjustments/error handling based on the timezone
    datetime_struct dts;
    dts.set_from_str(utf8_str, ectx->date_parse_order, ectx->century_window,
                     ectx->default_errmode);
    *reinterpret_cast<int64_t *>(data) = dts.to_ticks();
}

void datetime_type::get_cal(const char *DYND_UNUSED(arrmeta), const char *data,
                int32_t &out_year, int32_t &out_month, int32_t &out_day,
                int32_t &out_hour, int32_t &out_min, int32_t &out_sec, int32_t &out_tick) const
{
    datetime_struct dts;
    dts.set_from_ticks(*reinterpret_cast<const int64_t *>(data));
    out_year = dts.ymd.year;
    out_month = dts.ymd.month;
    out_day = dts.ymd.day;
    out_hour = dts.hmst.hour;
    out_min = dts.hmst.minute;
    out_sec = dts.hmst.second;
    out_tick = dts.hmst.tick;
}

void datetime_type::print_data(std::ostream& o,
                const char *DYND_UNUSED(arrmeta), const char *data) const
{
    datetime_struct dts;
    dts.set_from_ticks(*reinterpret_cast<const int64_t *>(data));
    // TODO: Handle distiction between printing abstract and UTC units
    o << dts.to_str();
}

void datetime_type::print_type(std::ostream& o) const
{
    if (m_timezone == tz_abstract) {
        o << "datetime";
    } else {
        o << "datetime[tz='";
        switch (m_timezone) {
            case tz_utc:
                o << "UTC";
                break;
            default:
                o << "(invalid " << (int32_t)m_timezone << ")";
                break;
        }
        o << "']";
    }
}

bool datetime_type::is_lossless_assignment(const ndt::type& dst_tp, const ndt::type& src_tp) const
{
    if (dst_tp.extended() == this) {
        if (src_tp.extended() == this) {
            return true;
        } else if (src_tp.get_type_id() == date_type_id) {
            // There is only one possibility for the datetime type (TODO: timezones!)
            return true;
        } else {
            return false;
        }
    } else {
        return false;
    }
}

bool datetime_type::operator==(const base_type& rhs) const
{
    if (this == &rhs) {
        return true;
    } else if (rhs.get_type_id() != datetime_type_id) {
        return false;
    } else {
        const datetime_type& r = static_cast<const datetime_type &>(rhs);
        // TODO: When "other" timezone data is supported, need to compare them too
        return m_timezone == r.m_timezone;
    }
}

size_t datetime_type::make_assignment_kernel(
    ckernel_builder *out, size_t offset_out, const ndt::type &dst_tp,
    const char *dst_arrmeta, const ndt::type &src_tp, const char *src_arrmeta,
    kernel_request_t kernreq, const eval::eval_context *ectx) const
{
    if (this == dst_tp.extended()) {
        if (src_tp == dst_tp) {
            return make_pod_typed_data_assignment_kernel(out, offset_out,
                            get_data_size(), get_data_alignment(), kernreq);
        } else if (src_tp.get_kind() == string_kind) {
            // Assignment from strings
            return make_string_to_datetime_assignment_kernel(
                out, offset_out, dst_tp, dst_arrmeta, src_tp, src_arrmeta,
                kernreq, ectx);
        } else if (src_tp.get_kind() == struct_kind) {
            // Convert to struct using the "struct" property
            return ::make_assignment_kernel(
                out, offset_out, ndt::make_property(dst_tp, "struct"),
                dst_arrmeta, src_tp, src_arrmeta, kernreq, ectx);
        } else if (!src_tp.is_builtin()) {
            return src_tp.extended()->make_assignment_kernel(
                out, offset_out, dst_tp, dst_arrmeta, src_tp, src_arrmeta,
                kernreq, ectx);
        }
    } else {
        if (dst_tp.get_kind() == string_kind) {
            // Assignment to strings
            return make_datetime_to_string_assignment_kernel(
                out, offset_out, dst_tp, dst_arrmeta, src_tp, src_arrmeta,
                kernreq, ectx);
        } else if (dst_tp.get_kind() == struct_kind) {
            // Convert to struct using the "struct" property
            return ::make_assignment_kernel(
                out, offset_out, dst_tp, dst_arrmeta,
                ndt::make_property(src_tp, "struct"), src_arrmeta, kernreq,
                ectx);
        }
        // TODO
    }

    stringstream ss;
    ss << "Cannot assign from " << src_tp << " to " << dst_tp;
    throw dynd::type_error(ss.str());
}


///////// properties on the type

//static pair<string, gfunc::callable> datetime_type_properties[] = {
//};

void datetime_type::get_dynamic_type_properties(const std::pair<std::string, gfunc::callable> **out_properties, size_t *out_count) const
{
    *out_properties = NULL; //datetime_type_properties;
    *out_count = 0; //sizeof(datetime_type_properties) / sizeof(datetime_type_properties[0]);
}

///////// functions on the type

static nd::array function_type_now(const ndt::type& DYND_UNUSED(dt)) {
    throw runtime_error("TODO: implement datetime.now function");
    //datetime_struct dts;
    //fill_current_local_datetime(&fields);
    //nd::array result = nd::empty(dt);
    //*reinterpret_cast<int64_t *>(result.get_readwrite_originptr()) = dt.to_ticks();
    // Make the result immutable (we own the only reference to the data at this point)
    //result.flag_as_immutable();
    //return result;
}

static nd::array function_type_construct(const ndt::type& DYND_UNUSED(dt),
                const nd::array& DYND_UNUSED(year),
                const nd::array& DYND_UNUSED(month),
                const nd::array& DYND_UNUSED(day))
{
    throw runtime_error("dynd type datetime __construct__");
    /*
    // TODO proper buffering
    nd::array year_as_int = year.ucast(ndt::make_type<int32_t>()).eval();
    nd::array month_as_int = month.ucast(ndt::make_type<int32_t>()).eval();
    nd::array day_as_int = day.ucast(ndt::make_type<int32_t>()).eval();
    nd::array result;

    array_iter<1,3> iter(ndt::make_datetime(), result, year_as_int, month_as_int, day_as_int);
    if (!iter.empty()) {
        datetime_struct dts;
        do {
            ymd.year = *reinterpret_cast<const int32_t *>(iter.data<1>());
            ymd.month = *reinterpret_cast<const int32_t *>(iter.data<2>());
            ymd.day = *reinterpret_cast<const int32_t *>(iter.data<3>());
            if (!ymd.is_valid()) {
                stringstream ss;
                ss << "invalid year/month/day " << ymd.year << "/" << ymd.month << "/" << ymd.day;
                throw runtime_error(ss.str());
            }
            *reinterpret_cast<int64_t *>(iter.data<0>()) = dts.to_ticks();
        } while (iter.next());
    }

    return result;
    */
}

void datetime_type::get_dynamic_type_functions(const std::pair<std::string, gfunc::callable> **out_functions, size_t *out_count) const
{
    static pair<string, gfunc::callable> datetime_type_functions[] = {
        pair<string, gfunc::callable>(
            "now", gfunc::make_callable(&function_type_now, "self")),
        pair<string, gfunc::callable>(
            "__construct__",
            gfunc::make_callable(&function_type_construct, "self", "year",
                                 "month", "day"))};

    *out_functions = datetime_type_functions;
    *out_count = sizeof(datetime_type_functions) / sizeof(datetime_type_functions[0]);
}

///////// properties on the nd::array

static nd::array property_ndo_get_date(const nd::array& n) {
    return n.replace_dtype(ndt::make_property(n.get_dtype(), "date"));
}

static nd::array property_ndo_get_year(const nd::array& n) {
    return n.replace_dtype(ndt::make_property(n.get_dtype(), "year"));
}

static nd::array property_ndo_get_month(const nd::array& n) {
    return n.replace_dtype(ndt::make_property(n.get_dtype(), "month"));
}

static nd::array property_ndo_get_day(const nd::array& n) {
    return n.replace_dtype(ndt::make_property(n.get_dtype(), "day"));
}

static nd::array property_ndo_get_hour(const nd::array& n) {
    return n.replace_dtype(ndt::make_property(n.get_dtype(), "hour"));
}

static nd::array property_ndo_get_minute(const nd::array& n) {
    return n.replace_dtype(ndt::make_property(n.get_dtype(), "minute"));
}

static nd::array property_ndo_get_second(const nd::array& n) {
    return n.replace_dtype(ndt::make_property(n.get_dtype(), "second"));
}

static nd::array property_ndo_get_microsecond(const nd::array& n) {
    return n.replace_dtype(ndt::make_property(n.get_dtype(), "microsecond"));
}

static nd::array property_ndo_get_tick(const nd::array& n) {
    return n.replace_dtype(ndt::make_property(n.get_dtype(), "tick"));
}

void datetime_type::get_dynamic_array_properties(const std::pair<std::string, gfunc::callable> **out_properties, size_t *out_count) const
{
    static pair<string, gfunc::callable> date_array_properties[] = {
        pair<string, gfunc::callable>(
            "date", gfunc::make_callable(&property_ndo_get_date, "self")),
        pair<string, gfunc::callable>(
            "year", gfunc::make_callable(&property_ndo_get_year, "self")),
        pair<string, gfunc::callable>(
            "month", gfunc::make_callable(&property_ndo_get_month, "self")),
        pair<string, gfunc::callable>(
            "day", gfunc::make_callable(&property_ndo_get_day, "self")),
        pair<string, gfunc::callable>(
            "hour", gfunc::make_callable(&property_ndo_get_hour, "self")),
        pair<string, gfunc::callable>(
            "minute", gfunc::make_callable(&property_ndo_get_minute, "self")),
        pair<string, gfunc::callable>(
            "second", gfunc::make_callable(&property_ndo_get_second, "self")),
        pair<string, gfunc::callable>(
            "microsecond",
            gfunc::make_callable(&property_ndo_get_microsecond, "self")),
        pair<string, gfunc::callable>(
            "tick", gfunc::make_callable(&property_ndo_get_tick, "self")), };

    *out_properties = date_array_properties;
    *out_count = sizeof(date_array_properties) / sizeof(date_array_properties[0]);
}

///////// functions on the nd::array

static nd::array function_ndo_to_struct(const nd::array& n) {
    return n.replace_dtype(ndt::make_property(n.get_dtype(), "struct"));
}

static nd::array function_ndo_strftime(const nd::array& n, const std::string& format) {
    // TODO: Allow 'format' itself to be an array, with broadcasting, etc.
    if (format.empty()) {
        throw runtime_error("format string for strftime should not be empty");
    }
    return n.replace_dtype(ndt::make_unary_expr(ndt::make_string(), n.get_dtype(),
                    make_strftime_kernelgen(format)));
}

void datetime_type::get_dynamic_array_functions(const std::pair<std::string, gfunc::callable> **out_functions, size_t *out_count) const
{
    static pair<string, gfunc::callable> date_array_functions[] = {
        pair<string, gfunc::callable>(
            "to_struct", gfunc::make_callable(&function_ndo_to_struct, "self")),
        pair<string, gfunc::callable>(
            "strftime",
            gfunc::make_callable(&function_ndo_strftime, "self", "format")), };

    *out_functions = date_array_functions;
    *out_count = sizeof(date_array_functions) / sizeof(date_array_functions[0]);
}

///////// property accessor kernels (used by property_type)

namespace {
    struct datetime_property_kernel_extra {
        ckernel_prefix base;
        const datetime_type *datetime_tp;

        typedef datetime_property_kernel_extra extra_type;

        static void destruct(ckernel_prefix *extra)
        {
            extra_type *e = reinterpret_cast<extra_type *>(extra);
            base_type_xdecref(e->datetime_tp);
        }
    };

    void get_property_kernel_struct_single(char *DYND_UNUSED(dst), const char *DYND_UNUSED(src),
                    ckernel_prefix *DYND_UNUSED(extra))
    {
        throw runtime_error("TODO: get_property_kernel_struct_single");
    }

    void set_property_kernel_struct_single(char *DYND_UNUSED(dst), const char *DYND_UNUSED(src),
                    ckernel_prefix *DYND_UNUSED(extra))
    {
        throw runtime_error("TODO: set_property_kernel_struct_single");
    }

    void get_property_kernel_date_single(char *dst, const char *src,
                    ckernel_prefix *extra)
    {
        const datetime_property_kernel_extra *e = reinterpret_cast<datetime_property_kernel_extra *>(extra);
        const datetime_type *dd = e->datetime_tp;
        datetime_tz_t tz = dd->get_timezone();
        if (tz == tz_utc || tz == tz_abstract) {
            int64_t days = *reinterpret_cast<const int64_t *>(src);
            if (days < 0) {
                days -= (DYND_TICKS_PER_DAY - 1);
            }
            days /= DYND_TICKS_PER_DAY;

            *reinterpret_cast<int32_t *>(dst) = static_cast<int32_t>(days);
        } else {
            throw runtime_error("datetime date property only implemented for UTC and abstract timezones");
        }
    }

    void get_property_kernel_time_single(char *dst, const char *src,
                    ckernel_prefix *extra)
    {
        const datetime_property_kernel_extra *e = reinterpret_cast<datetime_property_kernel_extra *>(extra);
        const datetime_type *dd = e->datetime_tp;
        datetime_tz_t tz = dd->get_timezone();
        if (tz == tz_utc || tz == tz_abstract) {
            int64_t ticks = *reinterpret_cast<const int64_t *>(src);
            ticks %= DYND_TICKS_PER_DAY;
            if (ticks < 0) {
                ticks += DYND_TICKS_PER_DAY;
            }
            *reinterpret_cast<int64_t *>(dst) = ticks;
        } else {
            throw runtime_error("datetime time property only implemented for UTC and abstract timezones");
        }
    }

    void get_property_kernel_year_single(char *dst, const char *src,
                    ckernel_prefix *extra)
    {
        const datetime_property_kernel_extra *e = reinterpret_cast<datetime_property_kernel_extra *>(extra);
        const datetime_type *dd = e->datetime_tp;
        datetime_tz_t tz = dd->get_timezone();
        if (tz == tz_utc || tz == tz_abstract) {
            date_ymd ymd;
            ymd.set_from_ticks(*reinterpret_cast<const int64_t *>(src));
            *reinterpret_cast<int32_t *>(dst) = ymd.year;
        } else {
            throw runtime_error("datetime property access only implemented for UTC and abstract timezones");
        }
    }

    void get_property_kernel_month_single(char *dst, const char *src,
                    ckernel_prefix *extra)
    {
        const datetime_property_kernel_extra *e = reinterpret_cast<datetime_property_kernel_extra *>(extra);
        const datetime_type *dd = e->datetime_tp;
        datetime_tz_t tz = dd->get_timezone();
        if (tz == tz_utc || tz == tz_abstract) {
            date_ymd ymd;
            ymd.set_from_ticks(*reinterpret_cast<const int64_t *>(src));
            *reinterpret_cast<int32_t *>(dst) = ymd.month;
        } else {
            throw runtime_error("datetime property access only implemented for UTC and abstract timezones");
        }
    }

    void get_property_kernel_day_single(char *dst, const char *src,
                    ckernel_prefix *extra)
    {
        const datetime_property_kernel_extra *e = reinterpret_cast<datetime_property_kernel_extra *>(extra);
        const datetime_type *dd = e->datetime_tp;
        datetime_tz_t tz = dd->get_timezone();
        if (tz == tz_utc || tz == tz_abstract) {
            date_ymd ymd;
            ymd.set_from_ticks(*reinterpret_cast<const int64_t *>(src));
            *reinterpret_cast<int32_t *>(dst) = ymd.day;
        } else {
            throw runtime_error("datetime property access only implemented for UTC and abstract timezones");
        }
    }

    void get_property_kernel_hour_single(char *dst, const char *src,
                    ckernel_prefix *extra)
    {
        const datetime_property_kernel_extra *e = reinterpret_cast<datetime_property_kernel_extra *>(extra);
        const datetime_type *dd = e->datetime_tp;
        datetime_tz_t tz = dd->get_timezone();
        if (tz == tz_utc || tz == tz_abstract) {
            int64_t hour =
                *reinterpret_cast<const int64_t *>(src) % DYND_TICKS_PER_DAY;
            if (hour < 0) {
                hour += DYND_TICKS_PER_DAY;
            }
            hour /= DYND_TICKS_PER_HOUR;
            *reinterpret_cast<int32_t *>(dst) = static_cast<int32_t>(hour);
        } else {
            throw runtime_error("datetime property access only implemented for UTC and abstract timezones");
        }
    }

    void get_property_kernel_minute_single(char *dst, const char *src,
                    ckernel_prefix *extra)
    {
        const datetime_property_kernel_extra *e = reinterpret_cast<datetime_property_kernel_extra *>(extra);
        const datetime_type *dd = e->datetime_tp;
        datetime_tz_t tz = dd->get_timezone();
        if (tz == tz_utc || tz == tz_abstract) {
            int64_t minute =
                *reinterpret_cast<const int64_t *>(src) % DYND_TICKS_PER_HOUR;
            if (minute < 0) {
                minute += DYND_TICKS_PER_HOUR;
            }
            minute /= DYND_TICKS_PER_MINUTE;
            *reinterpret_cast<int32_t *>(dst) = static_cast<int32_t>(minute);
        } else {
            throw runtime_error("datetime property access only implemented for UTC and abstract timezones");
        }
    }

    void get_property_kernel_second_single(char *dst, const char *src,
                    ckernel_prefix *extra)
    {
        const datetime_property_kernel_extra *e = reinterpret_cast<datetime_property_kernel_extra *>(extra);
        const datetime_type *dd = e->datetime_tp;
        datetime_tz_t tz = dd->get_timezone();
        if (tz == tz_utc || tz == tz_abstract) {
            int64_t second =
                *reinterpret_cast<const int64_t *>(src) % DYND_TICKS_PER_MINUTE;
            if (second < 0) {
                second += DYND_TICKS_PER_MINUTE;
            }
            second /= DYND_TICKS_PER_SECOND;
            *reinterpret_cast<int32_t *>(dst) = static_cast<int32_t>(second);
        } else {
            throw runtime_error("datetime property access only implemented for UTC and abstract timezones");
        }
    }

    void get_property_kernel_microsecond_single(char *dst, const char *src,
                    ckernel_prefix *extra)
    {
        const datetime_property_kernel_extra *e = reinterpret_cast<datetime_property_kernel_extra *>(extra);
        const datetime_type *dd = e->datetime_tp;
        datetime_tz_t tz = dd->get_timezone();
        if (tz == tz_utc || tz == tz_abstract) {
            int64_t microsecond =
                *reinterpret_cast<const int64_t *>(src) % DYND_TICKS_PER_SECOND;
            if (microsecond < 0) {
                microsecond += DYND_TICKS_PER_SECOND;
            }
            microsecond /= DYND_TICKS_PER_MICROSECOND;
            *reinterpret_cast<int32_t *>(dst) = static_cast<int32_t>(microsecond);
        } else {
            throw runtime_error("datetime property access only implemented for UTC and abstract timezones");
        }
    }

    void get_property_kernel_tick_single(char *dst, const char *src,
                                         ckernel_prefix *extra)
    {
        const datetime_property_kernel_extra *e = reinterpret_cast<datetime_property_kernel_extra *>(extra);
        const datetime_type *dd = e->datetime_tp;
        datetime_tz_t tz = dd->get_timezone();
        if (tz == tz_utc || tz == tz_abstract) {
            int64_t tick = *reinterpret_cast<const int64_t *>(src) % 10000000LL;
            if (tick < 0) {
                tick += 10000000LL;
            }
            *reinterpret_cast<int32_t *>(dst) = static_cast<int32_t>(tick);
        } else {
            throw runtime_error("datetime property access only implemented for UTC and abstract timezones");
        }
    }

    void get_property_kernel_hours_after_1970_single(char *dst, const char *src,
                                                     ckernel_prefix *DYND_UNUSED(extra))
    {
        int64_t ticks = *reinterpret_cast<const int64_t *>(src);
        if (ticks < 0) {
            ticks -= DYND_TICKS_PER_HOUR - 1;
        }
        *reinterpret_cast<int64_t *>(dst) = ticks / DYND_TICKS_PER_HOUR;
    }

    void set_property_kernel_hours_after_1970_single(char *dst, const char *src,
                                                     ckernel_prefix *DYND_UNUSED(extra))
    {
        int64_t hours = *reinterpret_cast<const int64_t *>(src);
        *reinterpret_cast<int64_t *>(dst) = hours * DYND_TICKS_PER_HOUR;
    }

    void get_property_kernel_minutes_after_1970_single(char *dst, const char *src,
                                                     ckernel_prefix *DYND_UNUSED(extra))
    {
        int64_t ticks = *reinterpret_cast<const int64_t *>(src);
        if (ticks < 0) {
            ticks -= DYND_TICKS_PER_MINUTE - 1;
        }
        *reinterpret_cast<int64_t *>(dst) = ticks / DYND_TICKS_PER_MINUTE;
    }

    void set_property_kernel_minutes_after_1970_single(char *dst, const char *src,
                                                     ckernel_prefix *DYND_UNUSED(extra))
    {
        int64_t minutes = *reinterpret_cast<const int64_t *>(src);
        *reinterpret_cast<int64_t *>(dst) = minutes * DYND_TICKS_PER_MINUTE;
    }

    void get_property_kernel_seconds_after_1970_single(char *dst, const char *src,
                                                     ckernel_prefix *DYND_UNUSED(extra))
    {
        int64_t ticks = *reinterpret_cast<const int64_t *>(src);
        if (ticks < 0) {
            ticks -= DYND_TICKS_PER_SECOND - 1;
        }
        *reinterpret_cast<int64_t *>(dst) = ticks / DYND_TICKS_PER_SECOND;
    }

    void set_property_kernel_seconds_after_1970_single(char *dst, const char *src,
                                                     ckernel_prefix *DYND_UNUSED(extra))
    {
        int64_t seconds = *reinterpret_cast<const int64_t *>(src);
        *reinterpret_cast<int64_t *>(dst) = seconds * DYND_TICKS_PER_SECOND;
    }

    void get_property_kernel_milliseconds_after_1970_single(char *dst, const char *src,
                                                     ckernel_prefix *DYND_UNUSED(extra))
    {
        int64_t ticks = *reinterpret_cast<const int64_t *>(src);
        if (ticks < 0) {
            ticks -= DYND_TICKS_PER_MILLISECOND - 1;
        }
        *reinterpret_cast<int64_t *>(dst) = ticks / DYND_TICKS_PER_MILLISECOND;
    }

    void set_property_kernel_milliseconds_after_1970_single(char *dst, const char *src,
                                                     ckernel_prefix *DYND_UNUSED(extra))
    {
        int64_t milliseconds = *reinterpret_cast<const int64_t *>(src);
        *reinterpret_cast<int64_t *>(dst) = milliseconds * DYND_TICKS_PER_MILLISECOND;
    }

    void get_property_kernel_microseconds_after_1970_single(char *dst, const char *src,
                                                     ckernel_prefix *DYND_UNUSED(extra))
    {
        int64_t ticks = *reinterpret_cast<const int64_t *>(src);
        if (ticks < 0) {
            ticks -= DYND_TICKS_PER_MICROSECOND - 1;
        }
        *reinterpret_cast<int64_t *>(dst) = ticks / DYND_TICKS_PER_MICROSECOND;
    }

    void set_property_kernel_microseconds_after_1970_single(char *dst, const char *src,
                                                     ckernel_prefix *DYND_UNUSED(extra))
    {
        int64_t microseconds = *reinterpret_cast<const int64_t *>(src);
        *reinterpret_cast<int64_t *>(dst) = microseconds * DYND_TICKS_PER_MICROSECOND;
    }

    void get_property_kernel_nanoseconds_after_1970_single(char *dst, const char *src,
                                                     ckernel_prefix *DYND_UNUSED(extra))
    {
        int64_t ticks = *reinterpret_cast<const int64_t *>(src);
        *reinterpret_cast<int64_t *>(dst) = ticks * DYND_NANOSECONDS_PER_TICK;
    }

    void set_property_kernel_nanoseconds_after_1970_single(char *dst, const char *src,
                                                     ckernel_prefix *DYND_UNUSED(extra))
    {
        int64_t nanoseconds = *reinterpret_cast<const int64_t *>(src);
        if (nanoseconds < 0) {
            nanoseconds -= DYND_NANOSECONDS_PER_TICK - 1;
        }
        *reinterpret_cast<int64_t *>(dst) = nanoseconds / DYND_NANOSECONDS_PER_TICK;
    }
} // anonymous namespace

namespace {
    enum date_properties_t {
        datetimeprop_struct,
        datetimeprop_date,
        datetimeprop_time,
        datetimeprop_year,
        datetimeprop_month,
        datetimeprop_day,
        datetimeprop_hour,
        datetimeprop_minute,
        datetimeprop_second,
        datetimeprop_microsecond,
        datetimeprop_tick,
        // These provide numpy interop
        datetimeprop_hours_after_1970,
        datetimeprop_minutes_after_1970,
        datetimeprop_seconds_after_1970,
        datetimeprop_milliseconds_after_1970,
        datetimeprop_microseconds_after_1970,
        datetimeprop_nanoseconds_after_1970,
    };
}

size_t datetime_type::get_elwise_property_index(const std::string& property_name) const
{
    if (property_name == "struct") {
        // A read/write property for accessing a datetime as a struct
        return datetimeprop_struct;
    } else if (property_name == "date") {
        return datetimeprop_date;
    } else if (property_name == "time") {
        return datetimeprop_time;
    } else if (property_name == "year") {
        return datetimeprop_year;
    } else if (property_name == "month") {
        return datetimeprop_month;
    } else if (property_name == "day") {
        return datetimeprop_day;
    } else if (property_name == "hour") {
        return datetimeprop_hour;
    } else if (property_name == "minute") {
        return datetimeprop_minute;
    } else if (property_name == "second") {
        return datetimeprop_second;
    } else if (property_name == "microsecond") {
        return datetimeprop_microsecond;
    } else if (property_name == "tick") {
        return datetimeprop_tick;
    } else if (property_name == "hours_after_1970") {
        return datetimeprop_hours_after_1970;
    } else if (property_name == "minutes_after_1970") {
        return datetimeprop_minutes_after_1970;
    } else if (property_name == "seconds_after_1970") {
        return datetimeprop_seconds_after_1970;
    } else if (property_name == "milliseconds_after_1970") {
        return datetimeprop_milliseconds_after_1970;
    } else if (property_name == "microseconds_after_1970") {
        return datetimeprop_microseconds_after_1970;
    } else if (property_name == "nanoseconds_after_1970") {
        return datetimeprop_nanoseconds_after_1970;
    } else {
        stringstream ss;
        ss << "dynd type " << ndt::type(this, true) << " does not have a kernel for property " << property_name;
        throw runtime_error(ss.str());
    }
}

ndt::type datetime_type::get_elwise_property_type(size_t property_index,
            bool& out_readable, bool& out_writable) const
{
    switch (property_index) {
        case datetimeprop_struct:
            out_readable = true;
            out_writable = true;
            return datetime_struct::type();
        case datetimeprop_date:
            out_readable = true;
            out_writable = false;
            return ndt::make_date();
        case datetimeprop_time:
            out_readable = true;
            out_writable = false;
            return ndt::make_time(m_timezone);
        case datetimeprop_hours_after_1970:
        case datetimeprop_minutes_after_1970:
        case datetimeprop_seconds_after_1970:
        case datetimeprop_milliseconds_after_1970:
        case datetimeprop_microseconds_after_1970:
        case datetimeprop_nanoseconds_after_1970:
            out_readable = true;
            out_writable = true;
            return ndt::make_type<int64_t>();
        default:
            out_readable = true;
            out_writable = false;
            return ndt::make_type<int32_t>();
    }
}

size_t datetime_type::make_elwise_property_getter_kernel(
                ckernel_builder *out, size_t offset_out,
                const char *DYND_UNUSED(dst_arrmeta),
                const char *DYND_UNUSED(src_arrmeta), size_t src_property_index,
                kernel_request_t kernreq, const eval::eval_context *DYND_UNUSED(ectx)) const
{
    offset_out = make_kernreq_to_single_kernel_adapter(out, offset_out, kernreq);
    datetime_property_kernel_extra *e = out->get_at<datetime_property_kernel_extra>(offset_out);
    switch (src_property_index) {
    case datetimeprop_struct:
        e->base.set_function<unary_single_operation_t>(
            &get_property_kernel_struct_single);
        break;
    case datetimeprop_date:
        e->base.set_function<unary_single_operation_t>(
            &get_property_kernel_date_single);
        break;
    case datetimeprop_time:
        e->base.set_function<unary_single_operation_t>(
            &get_property_kernel_time_single);
        break;
    case datetimeprop_year:
        e->base.set_function<unary_single_operation_t>(
            &get_property_kernel_year_single);
        break;
    case datetimeprop_month:
        e->base.set_function<unary_single_operation_t>(
            &get_property_kernel_month_single);
        break;
    case datetimeprop_day:
        e->base.set_function<unary_single_operation_t>(
            &get_property_kernel_day_single);
        break;
    case datetimeprop_hour:
        e->base.set_function<unary_single_operation_t>(
            &get_property_kernel_hour_single);
        break;
    case datetimeprop_minute:
        e->base.set_function<unary_single_operation_t>(
            &get_property_kernel_minute_single);
        break;
    case datetimeprop_second:
        e->base.set_function<unary_single_operation_t>(
            &get_property_kernel_second_single);
        break;
    case datetimeprop_microsecond:
        e->base.set_function<unary_single_operation_t>(
            &get_property_kernel_microsecond_single);
        break;
    case datetimeprop_tick:
        e->base.set_function<unary_single_operation_t>(
            &get_property_kernel_tick_single);
        break;
    case datetimeprop_hours_after_1970:
        e->base.set_function<unary_single_operation_t>(
            &get_property_kernel_hours_after_1970_single);
        break;
    case datetimeprop_minutes_after_1970:
        e->base.set_function<unary_single_operation_t>(
            &get_property_kernel_minutes_after_1970_single);
        break;
    case datetimeprop_seconds_after_1970:
        e->base.set_function<unary_single_operation_t>(
            &get_property_kernel_seconds_after_1970_single);
        break;
    case datetimeprop_milliseconds_after_1970:
        e->base.set_function<unary_single_operation_t>(
            &get_property_kernel_milliseconds_after_1970_single);
        break;
    case datetimeprop_microseconds_after_1970:
        e->base.set_function<unary_single_operation_t>(
            &get_property_kernel_microseconds_after_1970_single);
        break;
    case datetimeprop_nanoseconds_after_1970:
        e->base.set_function<unary_single_operation_t>(
            &get_property_kernel_nanoseconds_after_1970_single);
        break;
    default:
        stringstream ss;
        ss << "dynd datetime type given an invalid property index"
           << src_property_index;
        throw runtime_error(ss.str());
    }
    e->base.destructor = &datetime_property_kernel_extra::destruct;
    e->datetime_tp = static_cast<const datetime_type *>(ndt::type(this, true).release());
    return offset_out + sizeof(datetime_property_kernel_extra);
}

size_t datetime_type::make_elwise_property_setter_kernel(
                ckernel_builder *out, size_t offset_out,
                const char *DYND_UNUSED(dst_arrmeta), size_t dst_property_index,
                const char *DYND_UNUSED(src_arrmeta),
                kernel_request_t kernreq, const eval::eval_context *DYND_UNUSED(ectx)) const
{
    offset_out = make_kernreq_to_single_kernel_adapter(out, offset_out, kernreq);
    datetime_property_kernel_extra *e = out->get_at<datetime_property_kernel_extra>(offset_out);
    switch (dst_property_index) {
    case datetimeprop_struct:
        e->base.set_function<unary_single_operation_t>(
            &set_property_kernel_struct_single);
        break;
    case datetimeprop_hours_after_1970:
        e->base.set_function<unary_single_operation_t>(
            &set_property_kernel_hours_after_1970_single);
        break;
    case datetimeprop_minutes_after_1970:
        e->base.set_function<unary_single_operation_t>(
            &set_property_kernel_minutes_after_1970_single);
        break;
    case datetimeprop_seconds_after_1970:
        e->base.set_function<unary_single_operation_t>(
            &set_property_kernel_seconds_after_1970_single);
        break;
    case datetimeprop_milliseconds_after_1970:
        e->base.set_function<unary_single_operation_t>(
            &set_property_kernel_milliseconds_after_1970_single);
        break;
    case datetimeprop_microseconds_after_1970:
        e->base.set_function<unary_single_operation_t>(
            &set_property_kernel_microseconds_after_1970_single);
        break;
    case datetimeprop_nanoseconds_after_1970:
        e->base.set_function<unary_single_operation_t>(
            &set_property_kernel_nanoseconds_after_1970_single);
        break;
    default:
        stringstream ss;
        ss << "dynd datetime type given an invalid property index"
           << dst_property_index;
        throw runtime_error(ss.str());
    }
    e->base.destructor = &datetime_property_kernel_extra::destruct;
    e->datetime_tp = static_cast<const datetime_type *>(ndt::type(this, true).release());
    return offset_out + sizeof(datetime_property_kernel_extra);
}

const ndt::type& ndt::make_datetime()
{
    // Static instance of the type, which has a reference count > 0 for the
    // lifetime of the program. This static construction is inside a
    // function to ensure correct creation order during startup.
    static datetime_type dt(tz_abstract);
    static const ndt::type static_instance(&dt, true);
    return static_instance;
}
