; #include directive patterns for C.
;
; Capture (@path) the path token of a preproc_include. Two forms exist and are
; treated differently by resolution:
;   #include "util.h"   -> string_literal  — a local include, resolved against
;                          the indexed set relative to the including file.
;   #include <stdio.h>  -> system_lib_string — a system/external header, recorded
;                          but never linked (there is no indexed file for it).
; The extractor distinguishes the two by the captured node's type and strips the
; surrounding delimiters (quotes or angle brackets) to recover the bare path.
(preproc_include
  path: (string_literal) @path)

(preproc_include
  path: (system_lib_string) @path)
