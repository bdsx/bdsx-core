
extrn returnPoint:near;

_TEXT segment
makefunc_getout proc
		mov rsp, qword ptr[returnPoint];
		and rsp, -2;

		pop rcx;
		pop rbp;
		pop rsi;
		pop rdi;

		mov qword ptr[returnPoint], rcx;
		xor rax, rax;
		ret;
makefunc_getout endp
_TEXT ends

END