; Type-reference patterns for C.
;
; Capture (@type) every type_identifier — the way both a tagged aggregate and a
; typedef appear at a use site:
;   struct Point p;   -> struct_specifier name: (type_identifier "Point")
;   Celsius c;        -> (type_identifier "Celsius")
; The enclosing function is not constrained here (a type can appear in the return
; type, a parameter, or a local at any depth of the body); the extractor climbs
; to the enclosing function_definition and drops references outside one, just as
; it does for call sites. Resolution to a declared type node — filtering out the
; type_identifier at a type's own definition — happens in the indexer.
;
; Built-in types (`int`, `char`) are primitive_type nodes, not type_identifiers,
; so they are naturally excluded; USES_TYPE tracks user-defined types only.
(type_identifier) @type
