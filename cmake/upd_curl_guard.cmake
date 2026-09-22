# Build-time guard (steve, D2 2026-09-17): a libcurl built with HAVE_INET_PTON references the
# host inet_pton, which newlib on the Vita does not provide with working semantics; the link
# either fails late or resolves to something that silently misparses addresses. A good VitaSDK
# libcurl.a defines its own curlx_inet_pton and never references a bare inet_pton.
#
#   include(${UPD_CORE}/cmake/upd_curl_guard.cmake)
#   upd_check_curl_inet_pton()            # uses ${VITASDK}/arm-vita-eabi/lib/libcurl.a
#   upd_check_curl_inet_pton(<path.a>)    # or an explicit archive (used by the guard's own test)

function(upd_check_curl_inet_pton)
    if(ARGC GREATER 0)
        set(_lib "${ARGV0}")
    else()
        set(_lib "$ENV{VITASDK}/arm-vita-eabi/lib/libcurl.a")
    endif()
    if(NOT EXISTS "${_lib}")
        message(FATAL_ERROR "upd_curl_guard: no libcurl archive at ${_lib}")
    endif()

    find_program(_nm NAMES arm-vita-eabi-nm nm)
    if(NOT _nm)
        message(FATAL_ERROR "upd_curl_guard: no nm found")
    endif()
    execute_process(COMMAND "${_nm}" "${_lib}" OUTPUT_VARIABLE _syms ERROR_VARIABLE _nmerr
                    RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "upd_curl_guard: nm failed on ${_lib}: ${_nmerr}")
    endif()

    string(REGEX MATCHALL "[\r\n][ \t]*U inet_pton" _undef "${_syms}")
    string(REGEX MATCHALL "[0-9a-fA-F]+ T curlx_inet_pton" _have_curlx "${_syms}")
    if(_undef)
        list(LENGTH _undef _n)
        message(FATAL_ERROR
            "upd_curl_guard: ${_lib} references inet_pton (${_n} undefined reference(s)). "
            "This libcurl was built with HAVE_INET_PTON; rebuild it for the Vita without that "
            "define, or the updater's URL/address handling is not the code that was tested.")
    endif()
    if(NOT _have_curlx)
        message(FATAL_ERROR
            "upd_curl_guard: ${_lib} does not define curlx_inet_pton. This is not the VitaSDK "
            "libcurl the updater was verified against.")
    endif()
    message(STATUS "upd_curl_guard: ${_lib} ok (curlx_inet_pton defined, no inet_pton reference)")
endfunction()
