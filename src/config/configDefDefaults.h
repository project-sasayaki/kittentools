/* Include before a settings definition (.def) inside the initializer of its struct: each setting at its default. */
#define CFG_str(s, n, d, text) .n = d,
#define CFG_bool(s, n, d, text) .n = d,
#define CFG_int(s, n, d, text) .n = d,
#define CFG_hex(s, n, p, d, text) .n = d,
#define CFG_ipv4(s, n, a, b, c, d, text)                                      \
    .n = (unsigned int)(((unsigned char)d << 24) | ((unsigned char)c << 16) | \
                        ((unsigned char)b << 8) | (unsigned char)a),
