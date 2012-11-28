//
// Copyright (C) 2011-12, Dynamic NDArray Developers
// BSD 2-Clause License, see LICENSE.txt
//
// DEPRECATED (replacement in ndobject_iter.hpp)

#ifndef _RAW_ITERATION_HPP_
#define _RAW_ITERATION_HPP_

#include <algorithm>

#include <dynd/ndarray.hpp>
#include <dynd/shape_tools.hpp>

namespace dynd {

namespace detail {

    /** A simple metaprogram to indicate whether a value is within the bounds or not */
    template<int V, int V_START, int V_END>
    struct is_value_within_bounds_raw_iter {
        enum {value = (V >= V_START) && V < V_END};
    };

    template<int Nwrite, int Nread, int staticNDIM = 3>
    class raw_ndarray_iter_base {
        // contains all the ndim-sized vectors
        // 0: iterindex
        // 1: itershape
        // 2 ... (Nwrite + Nread) + 1: all the operand strides
        multi_shortvector<intptr_t, Nwrite + Nread + 2, staticNDIM> m_vectors;
    protected:
        int m_ndim;
        char *m_data[Nwrite + Nread];

        inline intptr_t& iterindex(int i) {
            return m_vectors.get(0, i);
        }

        inline const intptr_t& iterindex(int i) const {
            return m_vectors.get(0, i);
        }

        inline intptr_t& itershape(int i) {
            return m_vectors.get(1, i);
        }

        inline const intptr_t& itershape(int i) const {
            return m_vectors.get(1, i);
        }

        inline intptr_t* strides(int k) {
            return m_vectors.get_all()[k+2];
        }

        inline const intptr_t* strides(int k) const {
            return m_vectors.get_all()[k+2];
        }

        inline bool strides_can_coalesce(int i, int j) {
            intptr_t size = itershape(i);
            for (int k = 0; k < Nwrite + Nread; ++k) {
                if (strides(k)[i] * size != strides(k)[j]) {
                    return false;
                }
            }
            return true;
        }

        void init(int ndim, const intptr_t *shape, char **data,
                                const intptr_t **in_strides, const int *axis_perm)
        {
            m_ndim = ndim;
            m_vectors.init(ndim);

            for (int k = 0; k < Nwrite + Nread; ++k) {
                m_data[k] = data[k];
            }

            // Special case 0 and 1 dimensional arrays
            if (m_ndim == 0) {
                m_ndim = 1;
                // NOTE: This is ok, because shortvectors always have
                //       at least 1 element even if initialized with size = 0.
                iterindex(0) = 0;
                itershape(0) = 1;
                for (int k = 0; k < Nwrite + Nread; ++k) {
                    strides(k)[0] = 0;
                }
                return;
            } else if (m_ndim == 1) {
                intptr_t size = shape[0];
                iterindex(0) = 0;
                itershape(0) = size;
                // Always make the stride positive
                if (in_strides[0][0] >= 0) {
                    for (int k = 0; k < Nwrite + Nread; ++k) {
                        strides(k)[0] = in_strides[k][0];
                    }
                } else {
                    for (int k = 0; k < Nwrite + Nread; ++k) {
                        m_data[k][0] += in_strides[k][0] * (size - 1);
                        strides(k)[0] = -in_strides[k][0];
                    }
                }
                return;
            }

            // Copy the strides/shape into the iterator's internal variables
            for (int i = 0; i < m_ndim; ++i) {
                int p = axis_perm[i];
                itershape(i) = shape[p];
                for (int k = 0; k < Nwrite + Nread; ++k) {
                    strides(k)[i] = in_strides[k][p];
                }
            }

            // Reverse any axes where the first operand has a negative stride
            for (int i = 0; i < m_ndim; ++i) {
                intptr_t stride = strides(0)[i], size = itershape(i);

                if (stride < 0) {
                    m_data[0] += stride * (size - 1);
                    strides(0)[i] = -stride;
                    for (int k = 1; k < Nwrite + Nread; ++k) {
                        m_data[k] += strides(k)[i] * (size - 1);
                        strides(k)[i] = -strides(k)[i];
                    }
                }

                // Detect and handle a zero-size array
                if (size == 0) {
                    m_ndim = 1;
                    itershape(0) = 0;
                    for (int k = 0; k < Nwrite + Nread; ++k) {
                        strides(k)[0] = 0;
                    }
                    return;
                }
            }

            // Coalesce axes where possible
            int i = 0;
            for (int j = 1; j < m_ndim; ++j) {
                if (itershape(i) == 1) {
                    // Remove axis i
                    itershape(i) = itershape(j);
                    for (int k = 0; k < Nwrite + Nread; ++k) {
                        strides(k)[i] = strides(k)[j];
                    }
                } else if (itershape(j) == 1) {
                    // Remove axis j
                } else if (strides_can_coalesce(i, j)) {
                    // Coalesce axes i and j
                    itershape(i) *= itershape(j);
                } else {
                    // Can't coalesce, go to the next i
                    ++i;
                    itershape(i) = itershape(j);
                    for (int k = 0; k < Nwrite + Nread; ++k) {
                        strides(k)[i] = strides(k)[j];
                    }
                }
            }
            m_ndim = i+1;
            memset(&iterindex(0), 0, m_ndim * sizeof(intptr_t));
        }

    public:
        bool iternext() {
            int i = 1;
            for (; i < m_ndim; ++i) {
                intptr_t size = itershape(i);
                if (++iterindex(i) == size) {
                    iterindex(i) = 0;
                    for (int k = 0; k < Nwrite + Nread; ++k) {
                        m_data[k] -= (size - 1) * strides(k)[i];
                    }
                } else {
                    for (int k = 0; k < Nwrite + Nread; ++k) {
                        m_data[k] += strides(k)[i];
                    }
                    break;
                }
            }

            return i < m_ndim;
        }

        inline intptr_t innersize() const {
            return itershape(0);
        }

        template<int K>
        inline typename enable_if<is_value_within_bounds_raw_iter<K, 0, Nwrite + Nread>::value, intptr_t>::type
                                                                        innerstride() const {
            return strides(K)[0];
        }

        /**
         * Provide non-const access to the 'write' operands.
         */
        template<int K>
        inline typename enable_if<is_value_within_bounds_raw_iter<K, 0, Nwrite>::value, char *>::type data() {
            return m_data[K];
        }

        /**
         * Provide const access to all the operands.
         */
        template<int K>
        inline typename enable_if<is_value_within_bounds_raw_iter<K, 0, Nwrite + Nread>::value, const char *>::type
                                                                        data() const {
            return m_data[K];
        }

        /**
         * This causes the iterator to skip elements which are visited for the first time,
         * up to the requested skip count. The number of elements skipped is subtracted from
         * the skip count. If this function returns true, the caller should skip the first
         * element of its inner loop.
         *
         * @param inout_skip_count  The number of elements skipped is subtracted from this value.
         *                          If initially set to the number of elements in the result, this
         *                          becomes zero when there are no more elements to skip.
         * @returns  True if the first element in the inner loop should be skipped.
         */
        template<int K>
        inline typename enable_if<is_value_within_bounds_raw_iter<K, 0, Nwrite + Nread>::value, bool>::type
                                                    skip_first_visits(intptr_t& inout_skip_count) {
            const intptr_t *strides_array = strides(K), *iterindex_array = &iterindex(0);
            // First check all the indexes managed by the iterator
            for(int i = 1; i < m_ndim; ++i) {
                if (strides_array[i] == 0 && iterindex_array != 0) {
                    return false;
                }
            }
            // Two cases, either just the first element is skipped or they all are
            if (strides_array[0] == 0) {
                return true;
            } else {
                // Skip all the elements in this inner loop, and
                // proceed to the next iteration.
                inout_skip_count -= innersize();
                if (iternext()) {
                    return skip_first_visits<K>(inout_skip_count);
                } else {
                    return false;
                }
            }
        }

        /**
         * Prints out a debug dump of the object.
         */
        void debug_print(std::ostream& o) {
            o << "------ raw_ndarray_iter<" << Nwrite << ", " << Nread << ">\n";
            o << " ndim: " << m_ndim << "\n";
            o << " iterindex: ";
            for (int i = 0; i < m_ndim; ++i) o << iterindex(i) << " ";
            o << "\n";
            o << " itershape: ";
            print_shape(o, m_ndim, itershape());
            o << "\n";
            o << " data: ";
            for (int k = 0; k < Nwrite + Nread; ++k) o << (void *)m_data[k] << " ";
            o << "\n";
            for (int k = 0; k < Nwrite + Nread; ++k) {
                o << " strides[" << k << "]: ";
                for (int i = 0; i < m_ndim; ++i) o << strides(k)[i] << " ";
                o << "\n";
            }
            o << "------" << std::endl;
        }
    };
} // namespace detail

/**
 * Use raw_ndarray_iter<Nwrite, int Nread> to iterate over Nwrite output and Nread
 * input arrays simultaneously.
 */
template<int Nwrite, int Nread>
class raw_ndarray_iter;

template<>
class raw_ndarray_iter<0,1> : public detail::raw_ndarray_iter_base<0,1> {
public:
    raw_ndarray_iter(int ndim, const intptr_t *shape, const char *data, const intptr_t *strides)
    {
        // Sort the strides in ascending order according to the first operand
        shortvector<int> axis_perm(m_ndim);
        strides_to_axis_perm(m_ndim, strides, axis_perm.get());

        init(ndim, shape, const_cast<char **>(&data), &strides, axis_perm.get());
    }

    raw_ndarray_iter(const ndarray& arr)
    {
        const char *data = arr.get_readonly_originptr();
        const intptr_t *strides = arr.get_strides();
        int ndim = arr.get_ndim();

        // Sort the strides in ascending order according to the first operand
        shortvector<int> axis_perm(ndim);
        strides_to_axis_perm(ndim, strides, axis_perm.get());

        init(ndim, arr.get_shape(), const_cast<char **>(&data), &strides, axis_perm.get());
    }

};

/**
 * Iterator with one output operand.
 */
template<>
class raw_ndarray_iter<1,0> : public detail::raw_ndarray_iter_base<1,0> {
public:
    raw_ndarray_iter(int ndim, const intptr_t *shape, const char *data, const intptr_t *strides)
    {
        // Sort the strides in ascending order according to the first operand
        shortvector<int> axis_perm(ndim);
        strides_to_axis_perm(ndim, strides, axis_perm.get());

        init(ndim, shape, const_cast<char **>(&data), &strides, axis_perm.get());
    }

    raw_ndarray_iter(const ndarray& arr)
    {
        char *data = arr.get_readwrite_originptr();
        const intptr_t *strides = arr.get_strides();
        int ndim = arr.get_ndim();

        // Sort the strides in ascending order according to the first operand
        shortvector<int> axis_perm(ndim);
        strides_to_axis_perm(ndim, strides, axis_perm.get());

        init(ndim, arr.get_shape(), const_cast<char **>(&data), &strides, axis_perm.get());
    }

};

/**
 * Iterator with one output operand and one input operand.
 */
template<>
class raw_ndarray_iter<1,1> : public detail::raw_ndarray_iter_base<1,1> {
public:
    raw_ndarray_iter(int ndim, const intptr_t *shape,
                                char *dataA, const intptr_t *stridesA,
                                const char *dataB, const intptr_t *stridesB)
    {
        char *data[2] = {dataA, const_cast<char *>(dataB)};
        const intptr_t *strides[2] = {stridesA, stridesB};

        // Sort the strides in ascending order according to the first operand
        shortvector<int> axis_perm(ndim);
        strides_to_axis_perm(ndim, stridesA, axis_perm.get());

        init(ndim, shape, data, strides, axis_perm.get());
    }
    raw_ndarray_iter(int ndim, const intptr_t *shape,
                                char *dataA, const intptr_t *stridesA,
                                const char *dataB, const intptr_t *stridesB,
                                int *axis_perm)
    {
        char *data[2] = {dataA, const_cast<char *>(dataB)};
        const intptr_t *strides[2] = {stridesA, stridesB};

        init(ndim, shape, data, strides, axis_perm);
    }
    raw_ndarray_iter(int ndim, const intptr_t *shape,
                                const dtype& op0_dt, ndarray_node_ptr& op0, uint32_t op0_access_flags,
                                const ndarray_node_ptr& op1)
    {
        shortvector<intptr_t> strides(ndim);
        char *data[3];

        // Get the two operands as strided arrays
        data[1] = const_cast<char *>(op1->get_readonly_originptr());
        op1->get_right_broadcast_strides(ndim, strides.get());

        // Generate the axis_perm from the input strides, and use it to allocate the output
        shortvector<int> axis_perm(ndim);
        strides_to_axis_perm(ndim, strides.get(), axis_perm.get());
        op0 = make_strided_ndarray_node(op0_dt, ndim, shape, axis_perm.get(), op0_access_flags, NULL, NULL);
        // Because we just allocated this buffer, we can write to it even though it
        // might be marked as readonly because the src memory block is readonly
        data[0] = const_cast<char *>(op0->get_readonly_originptr());

        const intptr_t *strides_ptrs[2] = {op0->get_strides(), strides.get()};
        init(ndim, shape, data, strides_ptrs, axis_perm.get());
    }
};

/**
 * Iterator with one output operand and two input operand.
 */
template<>
class raw_ndarray_iter<1,2> : public detail::raw_ndarray_iter_base<1,2> {
public:
    raw_ndarray_iter(int ndim, const intptr_t *shape,
                                char *dataA, const intptr_t *stridesA,
                                const char *dataB, const intptr_t *stridesB,
                                const char *dataC, const intptr_t *stridesC)
    {
        char *data[3] = {dataA, const_cast<char *>(dataB), const_cast<char *>(dataC)};
        const intptr_t *strides[3] = {stridesA, stridesB, stridesC};

        // Sort the strides in ascending order according to the first operand
        shortvector<int> axis_perm(ndim);
        strides_to_axis_perm(ndim, stridesA, axis_perm.get());

        init(ndim, shape, data, strides, axis_perm.get());
    }

    raw_ndarray_iter(int ndim, const intptr_t *shape,
                                const dtype& op0_dt, ndarray_node_ptr& op0, uint32_t op0_access_flags,
                                ndarray_node *op1,
                                ndarray_node *op2)
    {
        if (op0_dt.kind() == expression_kind) {
            std::stringstream ss;
            ss << "raw_ndarray_iter: to automatically allocate an output, must not use expression dtype " << op0_dt << ", should use its value dtype";
            throw std::runtime_error(ss.str());
        }
        multi_shortvector<intptr_t, 2> strides_vec(ndim);
        char *data[3];

        // Get the two operands as strided arrays
        data[1] = const_cast<char *>(op1->get_readonly_originptr());
        op1->get_right_broadcast_strides(ndim, strides_vec.get(0));
        data[2] = const_cast<char *>(op2->get_readonly_originptr());
        op2->get_right_broadcast_strides(ndim, strides_vec.get(1));

        // Generate the axis_perm from the input strides, and use it to allocate the output
        shortvector<int> axis_perm(ndim);
        multistrides_to_axis_perm(ndim, 2, strides_vec.get_all(), axis_perm.get());
        op0 = make_strided_ndarray_node(op0_dt, ndim, shape, axis_perm.get(), op0_access_flags, NULL, NULL);
        // Because we just allocated this buffer, we can write to it even though it
        // might be marked as readonly because the src memory block is readonly
        data[0] = const_cast<char *>(op0->get_readonly_originptr());

        const intptr_t *strides_ptrs[3] = {op0->get_strides(), strides_vec.get(0), strides_vec.get(1)};
        init(ndim, shape, data, strides_ptrs, axis_perm.get());
    }

    /**
     * Iterator for 2 input operands, output allocated by the iterator.
     *
     * Constructs the iterator with output 'op0' and two inputs 'op1' and 'op2'. This constructor
     * resets op0 to a new array with the dtype 'op0_dt', a shape matching the input broadcast
     * shape, and a memory layout matching the inputs as closely as possible.
     */
    raw_ndarray_iter(const dtype& op0_dt, ndarray& op0, const ndarray& op1, const ndarray& op2) {
        if (op0_dt.kind() == expression_kind) {
            std::stringstream ss;
            ss << "raw_ndarray_iter: to automatically allocate an output, must not use expression dtype " << op0_dt << ", should use its value dtype";
            throw std::runtime_error(ss.str());
        }
        // Broadcast the input shapes together
        int ndim;
        dimvector op0shape;
        broadcast_input_shapes(op1.get_node(), op2.get_node(), &ndim, &op0shape);

        // Create the broadcast strides
        multi_shortvector<intptr_t, 3> strides_vec(ndim);
        intptr_t **strides = strides_vec.get_all();
        copy_input_strides(op1, ndim, strides[1]);
        copy_input_strides(op2, ndim, strides[2]);

        // Generate the axis_perm from the input strides, and use it to allocate the output
        shortvector<int> axis_perm(ndim);
        multistrides_to_axis_perm(ndim, 2, strides + 1, axis_perm.get());
        op0 = ndarray(op0_dt, ndim, op0shape.get(), axis_perm.get());
        copy_input_strides(op0, ndim, strides[0]);

        char *data[3] = {op0.get_readwrite_originptr(),
                        const_cast<char *>(op1.get_readonly_originptr()),
                        const_cast<char *>(op2.get_readonly_originptr())};

        init(ndim, op0shape.get(), data, const_cast<const intptr_t **>(strides), axis_perm.get());
    }
};

} // namespace dynd

#endif // _RAW_ITERATION_HPP_
