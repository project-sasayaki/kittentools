#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A card went into the reader. newCard: just dispensed, so nothing is printed on it. */
void card_print_card_inserted(const char *cardPath, bool newCard);
void card_print_card_removed(void);

/* The data of each print command, as it came in the frame. */
void card_print_settings(const uint8_t *data, size_t length);       /* 78h */
void card_print_register_glyph(const uint8_t *data, size_t length); /* 7Ah */
void card_print_text(const uint8_t *data, size_t length);           /* 7Ch */
void card_print_erase(const uint8_t *data, size_t length);          /* 7Dh */
