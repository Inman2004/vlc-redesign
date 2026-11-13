/*
 * ebml_compat.S : libebml's EbmlString string conversion under GCC 16 mangling
 *
 * VideoLAN's prebuilt libebml was built by a GCC that put the [abi:cxx11]
 * tag in the name of `EbmlString::operator const std::string &() const`;
 * GCC 16 leaves it out. Same function, same ABI: forward the new name.
 *
 * Build-time only: linked into the Windows build of VLC, not part of VLC.
 */
    .text
    .globl  _ZNK7libebml10EbmlStringcvRKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEEv
    .def    _ZNK7libebml10EbmlStringcvRKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEEv; .scl 2; .type 32; .endef
_ZNK7libebml10EbmlStringcvRKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEEv:
    jmp     _ZNK7libebml10EbmlStringcvRKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEB5cxx11Ev
