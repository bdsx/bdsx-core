
extrn returnPoint:near;

_TEXT segment
makefunc_getout proc
		mov rdx, qword ptr[returnPoint];
		mov rsp, qword ptr[rdx];
		pop rcx;
		pop rbp;
		pop rsi;
		pop rdi;

		mov qword ptr[rdx], rcx;
		xor rax, rax;
		ret;
makefunc_getout endp
_TEXT ends

END