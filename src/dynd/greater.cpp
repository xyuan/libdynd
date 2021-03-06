//
// Copyright (C) 2011-16 DyND Developers
// BSD 2-Clause License, see LICENSE.txt
//

#include <dynd/callables/greater_callable.hpp>
#include <dynd/comparison_common.hpp>

using namespace std;
using namespace dynd;

namespace {
nd::callable make_greater() { return make_comparison_callable<nd::greater_callable>(); }
}

DYND_API nd::callable nd::greater = make_greater();
