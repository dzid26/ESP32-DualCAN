#include "be_constobj.h"

static be_define_const_map_slots(m_libintrospect_map) {
    { be_const_key(fromptr, 9), be_const_func(m_fromptr) },
    { be_const_key(set, 8), be_const_func(m_setmember) },
    { be_const_key(name, 10), be_const_func(m_name) },
    { be_const_key(setmodule, -1), be_const_func(m_setmodule) },
    { be_const_key(module, -1), be_const_func(m_getmodule) },
    { be_const_key(get, 7), be_const_func(m_findmember) },
    { be_const_key(ismethod, -1), be_const_func(m_ismethod) },
    { be_const_key(toptr, -1), be_const_func(m_toptr) },
    { be_const_key(solidified, -1), be_const_func(m_solidified) },
    { be_const_key(members, -1), be_const_func(m_attrlist) },
    { be_const_key(contains, -1), be_const_func(m_contains) },
};

static be_define_const_map(
    m_libintrospect_map,
    11
);

static be_define_const_module(
    m_libintrospect,
    "introspect"
);

BE_EXPORT_VARIABLE be_define_const_native_module(introspect);
