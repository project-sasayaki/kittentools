/* Include before a settings definition (.def) inside a struct: one field for each setting. */
#define CFG_str(s, n, d, text) char *n;
#define CFG_bool(s, n, d, text) bool n;
#define CFG_int(s, n, d, text) int n;
#define CFG_hex(s, n, p, d, text) int n;
#define CFG_ipv4(s, n, a, b, c, d, text) unsigned int n;
