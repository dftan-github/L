/* Phase-1 portable mini-regex (no POSIX dependency — works on MSVC/Windows).
 *
 * Supports: literals, `.`, character classes `[a-z]` / `[0-9]` / …,
 * quantifiers `*` `+` `?` on the preceding atom. Search anywhere in `text`.
 * Returns 1 on match, 0 otherwise.
 */
#include <stdint.h>
#include <string.h>

static int match_here(const char* pat, const char* text);

static int is_meta(char c) {
  return c == '*' || c == '+' || c == '?' || c == '[' || c == '.' || c == '\\';
}

/* Parse one atom into *out_end (points after atom). Returns 1 if atom OK. */
static int parse_atom(const char* pat, const char** out_end) {
  if (pat == NULL || *pat == '\0') {
    return 0;
  }
  if (*pat == '\\') {
    if (pat[1] == '\0') {
      return 0;
    }
    *out_end = pat + 2;
    return 1;
  }
  if (*pat == '[') {
    const char* p = pat + 1;
    if (*p == '^') {
      ++p;
    }
    if (*p == '\0') {
      return 0;
    }
    /* Allow empty-looking class only if ']' is escaped somehow — require ≥1 char. */
    if (*p == ']') {
      ++p;
    }
    while (*p != '\0' && *p != ']') {
      ++p;
    }
    if (*p != ']') {
      return 0;
    }
    *out_end = p + 1;
    return 1;
  }
  /* `.` or literal */
  *out_end = pat + 1;
  return 1;
}

static int char_in_class(char ch, const char* cls_open) {
  /* cls_open points at '[' */
  const char* p = cls_open + 1;
  int negated = 0;
  if (*p == '^') {
    negated = 1;
    ++p;
  }
  int matched = 0;
  if (*p == ']') {
    if (ch == ']') {
      matched = 1;
    }
    ++p;
  }
  while (*p != '\0' && *p != ']') {
    if (p[1] == '-' && p[2] != '\0' && p[2] != ']') {
      char lo = p[0];
      char hi = p[2];
      if (lo <= ch && ch <= hi) {
        matched = 1;
      }
      p += 3;
    } else {
      if (*p == ch) {
        matched = 1;
      }
      ++p;
    }
  }
  return negated ? !matched : matched;
}

static int match_atom_once(const char* atom, const char* atom_end, const char* text,
                           const char** text_out) {
  if (*text == '\0') {
    return 0;
  }
  char ch = *text;
  if (atom[0] == '\\') {
    if (atom[1] != ch) {
      return 0;
    }
    *text_out = text + 1;
    return 1;
  }
  if (atom[0] == '.') {
    *text_out = text + 1;
    return 1;
  }
  if (atom[0] == '[') {
    if (!char_in_class(ch, atom)) {
      return 0;
    }
    *text_out = text + 1;
    return 1;
  }
  if (atom[0] != ch) {
    return 0;
  }
  *text_out = text + 1;
  return 1;
}

static int match_star(const char* atom, const char* atom_end, const char* rest,
                      const char* text) {
  /* Greedy: consume as many as possible, then backtrack. */
  const char* t = text;
  const char* positions[256];
  int npos = 0;
  positions[npos++] = t;
  while (npos < 256) {
    const char* next = NULL;
    if (!match_atom_once(atom, atom_end, t, &next)) {
      break;
    }
    t = next;
    positions[npos++] = t;
  }
  for (int i = npos - 1; i >= 0; --i) {
    if (match_here(rest, positions[i])) {
      return 1;
    }
  }
  return 0;
}

static int match_here(const char* pat, const char* text) {
  if (pat == NULL) {
    return 0;
  }
  if (*pat == '\0') {
    return 1;
  }

  const char* atom_end = NULL;
  if (!parse_atom(pat, &atom_end)) {
    return 0;
  }
  const char* atom = pat;
  char quant = '\0';
  const char* rest = atom_end;
  if (*atom_end == '*' || *atom_end == '+' || *atom_end == '?') {
    quant = *atom_end;
    rest = atom_end + 1;
  }

  if (quant == '*') {
    return match_star(atom, atom_end, rest, text);
  }
  if (quant == '+') {
    const char* next = NULL;
    if (!match_atom_once(atom, atom_end, text, &next)) {
      return 0;
    }
    return match_star(atom, atom_end, rest, next);
  }
  if (quant == '?') {
    const char* next = NULL;
    if (match_atom_once(atom, atom_end, text, &next)) {
      if (match_here(rest, next)) {
        return 1;
      }
    }
    return match_here(rest, text);
  }

  const char* next = NULL;
  if (!match_atom_once(atom, atom_end, text, &next)) {
    return 0;
  }
  return match_here(rest, next);
}

int32_t l_regex_match(const char* pattern, const char* text) {
  if (pattern == NULL || text == NULL) {
    return 0;
  }
  /* Empty pattern matches empty or any (treat as match everywhere). */
  if (pattern[0] == '\0') {
    return 1;
  }
  for (const char* t = text;; ++t) {
    if (match_here(pattern, t)) {
      return 1;
    }
    if (*t == '\0') {
      break;
    }
  }
  return 0;
}
