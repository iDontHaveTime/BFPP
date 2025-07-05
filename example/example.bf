?extern putchar
?extern puts

; This is brainfuck++ a superset of brainfuck.
; it has all the basic brainfuck ops but it adds a ton more.

@main:i32 ; you can specify the function return type, if you dont its void by default.
    ?call another_func ; return values go into the CURRENT ADDRESS with the CURRENT SIZE.
    ; IF YOU NEED SOMETHING LIKE MALLOC MAKE SURE YOU ARE IN 64BIT MODE!

    ++++>+++>>--- ; 3
    >?mov 10[-]

    ?i32 ; you can definitely switch modes, big feature aint it!

    >> ; 11 here we switched to i32 aka 4 bytes

    ?i8 ; here we are back to 1 byte

    << ; 9 go back 2 addresses, our mode is i8 as previously switched

    ; unlike normal BF there is NO guarantee that the tape fully starts at 0
    ; in fact it will NEVER start at 0
    ; you might ask how will I zero it out, via []?
    ; technically you can, but its not efficient, so I introduce you this
    ?mov 0 ; moving straight into the cell

    >>>>>>> ; gotta be 16 aligned for syscall (recommended at least)
    ?i64
    ?mov 0 ; be careful, it IS gonna zero out addresses in front of it (depends on mode)

    ; this moves 0 to the current address
    ; ? actually means BF++ exclusive ops

    ?mov 0x41 ; moves 65 to the current address, much better than a loop
    ... ; will output AAA (on linux)

    ?mov 0x0a
    .
    ?i32
    >>?mov 0

    ! ; moves current address pointer to rax/eax
    ?i8 ; go back, this applies globally

@another_func
    ?i64

    ?mov 104
    * ; star means it puts it into argument, the amount of stars the argument it is
    ?call putchar
    ?i8


    !

@arg_func:void
    ?i64
    ; you can use & for accepting arguments, the & moves the argument into the tape
    &>&&>&&& ; this accepts first 3 arguments

    ; you can exploit this feature to use it as "checkpoints" for example (ALWAYS USE 64BIT HERE!)
    >>*^ ; save current position

    <<<<<< ; move back

    &^ ; jump back to first argument aka our saved position via *

    ; basically &^  means that argument into address pointer
    ; and *^ means put address pointer into argument

    ; when . (syscall) is used, most registers will not contain stored values anymore, checkpoints for example


    ; explicit return is optional