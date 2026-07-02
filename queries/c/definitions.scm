; Function definition patterns for C.
;
; Capture (@name) the identifier at the base of the declarator chain of a
; function_definition. Two patterns cover the common return-type shapes:
;   int  foo(void) { ... }   -> declarator is a function_declarator
;   char *foo(void) { ... }  -> declarator is a pointer_declarator wrapping one
; Deeper pointer nesting (e.g. `char **foo(void)`) is a known, intentional gap
; per ADR-0001 (favor a simple design; record edge cases as limitations).
(function_definition
  declarator: (function_declarator
                declarator: (identifier) @name))

(function_definition
  declarator: (pointer_declarator
                declarator: (function_declarator
                              declarator: (identifier) @name)))
