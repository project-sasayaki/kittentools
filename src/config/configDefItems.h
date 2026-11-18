/* Include before a settings definition (.def) inside a ConfigItem array: one item for each of its lines.
 * The includer defines CONFIG_DEF_OFFSET(section, name): where the setting's value is in the table's values. */
#define SECTION(s, text) {.type = CONFIG_ITEM_SECTION, .section = #s, .comment = text},
#define COMMENT(text) {.type = CONFIG_ITEM_COMMENT, .comment = text},
#define NEWLINE() {.type = CONFIG_ITEM_NEWLINE},
#define GROUP(g) {.type = CONFIG_ITEM_GROUP, .group = g},
#define CFG_str(s, n, d, text)                                           \
    {.type = CONFIG_ITEM_STR, .section = #s, .name = #n, .comment = text, \
     .offset = CONFIG_DEF_OFFSET(s, n), .defaultStr = d},
#define CFG_bool(s, n, d, text)                                           \
    {.type = CONFIG_ITEM_BOOL, .section = #s, .name = #n, .comment = text, \
     .offset = CONFIG_DEF_OFFSET(s, n), .defaultInt = d},
#define CFG_int(s, n, d, text)                                           \
    {.type = CONFIG_ITEM_INT, .section = #s, .name = #n, .comment = text, \
     .offset = CONFIG_DEF_OFFSET(s, n), .defaultInt = d},
#define CFG_hex(s, n, p, d, text)                                        \
    {.type = CONFIG_ITEM_HEX, .section = #s, .name = #n, .comment = text, \
     .offset = CONFIG_DEF_OFFSET(s, n), .precision = p, .defaultInt = d},
#define CFG_ipv4(s, n, a, b, c, d, text)                                                  \
    {.type = CONFIG_ITEM_IPV4, .section = #s, .name = #n, .comment = text,                 \
     .offset = CONFIG_DEF_OFFSET(s, n),                                                    \
     .defaultIpv4 = (unsigned int)(((unsigned char)d << 24) | ((unsigned char)c << 16) |   \
                                   ((unsigned char)b << 8) | (unsigned char)a)},
#define RENAMED_int(old_s, old_n, s, n) \
    {.type = CONFIG_ITEM_RENAMED_INT, .section = #old_s, .name = #old_n, .offset = CONFIG_DEF_OFFSET(s, n)},
#define RENAMED_str(old_s, old_n, s, n) \
    {.type = CONFIG_ITEM_RENAMED_STR, .section = #old_s, .name = #old_n, .offset = CONFIG_DEF_OFFSET(s, n)},
