( Implement Vocabularies )
variable last-vocabulary
current @ constant forth-wordlist
: forth   forth-wordlist context ! ;
: vocabulary ( "name" ) create 0 , current @ 2 cells + , current @ @ last-vocabulary !
                        does> cell+ context ! ;
: definitions   context @ current ! ;
: >name-length ( xt -- n ) dup 0= if exit then >name nip ;
: vlist   0 context @ @ begin dup >name-length while onlines dup see. >link repeat 2drop cr ;

( Make it easy to transfer words between vocabularies )
: transfer-xt ( xt --  ) context @ begin 2dup @ <> while @ >link& repeat nip
                         dup @ swap dup @ >link swap ! current @ @ over >link& !   current @ ! ;
: transfer ( "name" ) ' transfer-xt ;
: ?transfer ( "name" ) bl parse find dup if transfer-xt else drop then ;
: }transfer ;
: transfer{ begin ' dup ['] }transfer = if drop exit then transfer-xt again ;

( Watered down versions of these )
: only   forth 0 context cell+ ! ;
: voc-stack-end ( -- a ) context begin dup @ while cell+ repeat ;
: also   context context cell+ voc-stack-end over - 2 cells + cmove> ;
: sealed   0 last-vocabulary @ >body cell+ ! ;
: voc. ( voc -- ) dup forth-wordlist = if ." FORTH " drop exit then 3 cells - see. ;
: order   context begin dup @ while dup @ voc. cell+ repeat drop cr ;

( Hide some words in an internals vocabulary )
vocabulary internals   internals definitions
transfer{
  transfer-xt voc-stack-end forth-wordlist voc.
  last-vocabulary
  branch 0branch donext dolit
  'context 'notfound notfound
  immediate? input-buffer ?echo ?echo-prompt
  evaluate1 evaluate-buffer
  'sys 'heap aliteral
  leaving( )leaving leaving leaving,
  (do) (?do) (+loop)
  parse-quote digit $@
  see. see-loop >name-length exit=
  see-one raw.s
  tib-setup input-limit
}transfer
forth definitions

