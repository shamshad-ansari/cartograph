; Aggregate- and user-defined-type declaration patterns for C.
;
; Capture (@name) the identifier that names a type *definition* — the tag of a
; struct/union/enum that carries a body, or the alias introduced by a typedef.
; The kind (Struct/Union/Enum/Typedef) is recovered by the extractor from the
; captured node's parent, so a single capture name covers all four.
;
;   struct Point { ... };   -> struct_specifier with a name + body
;   union  Value { ... };   -> union_specifier
;   enum   Color { ... };   -> enum_specifier
;   typedef int Celsius;    -> type_definition, name is the declarator
;
; Requiring a body on the aggregate patterns keeps bare references (`struct
; Point p;`, which has no body) out — those are uses, matched by type_uses.scm.
; An anonymous aggregate (`typedef struct { ... } Point;`) has no name field and
; so yields only the typedef, which is the name C code actually refers to.
;
; A typedef whose declarator is a pointer (`typedef struct X *XPtr;`) sits under
; a pointer_declarator and is a known, intentional gap, mirroring the pointer
; handling in definitions.scm (ADR-0001).
(struct_specifier
  name: (type_identifier) @name
  body: (field_declaration_list))

(union_specifier
  name: (type_identifier) @name
  body: (field_declaration_list))

(enum_specifier
  name: (type_identifier) @name
  body: (enumerator_list))

(type_definition
  declarator: (type_identifier) @name)
