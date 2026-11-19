/* First line of a settings definition (.def): the entries its includer did not define are left out. */
#ifndef SECTION
#define SECTION(s, text)
#endif
#ifndef ENDSECTION
#define ENDSECTION(s)
#endif
#ifndef COMMENT
#define COMMENT(text)
#endif
#ifndef NEWLINE
#define NEWLINE()
#endif
#ifndef GROUP
#define GROUP(g)
#endif
#ifndef CFG_str
#define CFG_str(s, n, d, text)
#endif
#ifndef CFG_bool
#define CFG_bool(s, n, d, text)
#endif
#ifndef CFG_int
#define CFG_int(s, n, d, text)
#endif
#ifndef CFG_hex
#define CFG_hex(s, n, p, d, text)
#endif
#ifndef CFG_ipv4
#define CFG_ipv4(s, n, a, b, c, d, text)
#endif
#ifndef RENAMED_int
#define RENAMED_int(old_s, old_n, s, n)
#endif
#ifndef RENAMED_str
#define RENAMED_str(old_s, old_n, s, n)
#endif
