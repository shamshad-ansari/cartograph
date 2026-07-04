; Function declaration (prototype) patterns for C.
;
; A prototype is a `declaration` whose declarator is a function_declarator —
; `int foo(int);` — as opposed to a function_definition, which owns a body and
; is handled by definitions.scm. Capture (@name) the identifier at the base of
; the declarator chain. Two patterns cover the common return-type shapes:
;   int   foo(int);   -> declarator is a function_declarator
;   char *foo(void);  -> declarator is a pointer_declarator wrapping one
; Deeper pointer nesting mirrors the same intentional gap as definitions.scm
; (ADR-0001). Constraining the base to an (identifier) keeps function-pointer
; variable declarations — whose name sits under a parenthesized_declarator — out.
(declaration
  declarator: (function_declarator
                declarator: (identifier) @name))

(declaration
  declarator: (pointer_declarator
                declarator: (function_declarator
                              declarator: (identifier) @name)))
