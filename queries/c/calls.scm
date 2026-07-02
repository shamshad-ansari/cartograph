; Call-site patterns for C.
;
; Capture (@callee) the identifier in call position — the direct `foo(...)`
; form. The enclosing caller is not constrained here: calls appear at any depth
; inside control flow, which a fixed nesting pattern could not express, so the
; extractor climbs to the enclosing function_definition itself.
;
; Calls through function pointers (`(*fp)()`) or struct fields (`s.fn()`) have a
; non-identifier in call position and are intentionally out of scope for this
; naive, name-based slice (ADR-0001; linkage rules arrive in 0004).
(call_expression
  function: (identifier) @callee)
