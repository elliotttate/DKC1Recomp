#include "dkc1_script.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum Dkc1ScriptOpKind {
  kOpInput,      /* mask for count frames */
  kOpWait,       /* neutral/held mask until predicate or timeout */
  kOpCheckpoint,
  kOpStateSave,
  kOpStateLoad,
};

enum { kDkc1ScriptMaxSteps = 4096, kDkc1ScriptDefaultTimeout = 3600 };

typedef struct Dkc1ScriptStep {
  int kind;
  uint32_t input_mask;
  long count;           /* kOpInput frames, or wait timeout budget */
  uint16_t address;
  uint16_t value;
  uint16_t value_mask;
  char op[3];
  char *text;           /* checkpoint name / state path */
  long line;
} Dkc1ScriptStep;

static Dkc1ScriptStep s_steps[kDkc1ScriptMaxSteps];
static size_t s_step_count;
static size_t s_cursor;
static long s_step_progress;
static bool s_active;
static bool s_failed;
static char s_error[256];

static char *Trim(char *text) {
  while (*text && isspace((unsigned char)*text)) text++;
  char *end = text + strlen(text);
  while (end > text && isspace((unsigned char)end[-1])) *--end = '\0';
  return text;
}

static void SetError(const char *message, long line) {
  if (line > 0)
    snprintf(s_error, sizeof s_error, "line %ld: %s", line, message);
  else
    snprintf(s_error, sizeof s_error, "%s", message);
}

const char *Dkc1ScriptError(void) {
  return s_error;
}

static bool ParseUnsigned(const char *token, unsigned long *value, int base) {
  char *end = NULL;
  unsigned long parsed = strtoul(token, &end, base);
  if (!end || end == token || *end != '\0')
    return false;
  *value = parsed;
  return true;
}

static bool ParsePredicate(char **tokens, int token_count,
                           Dkc1ScriptStep *step, long line) {
  /* ADDR OP VALUE [mask M] [timeout N] */
  unsigned long address, value;
  if (token_count < 3 || !ParseUnsigned(tokens[0], &address, 16) ||
      address > 0x1FFFEul) {
    SetError("expected hex WRAM address", line);
    return false;
  }
  const char *op = tokens[1];
  if (strcmp(op, "==") && strcmp(op, "!=") && strcmp(op, ">=") &&
      strcmp(op, "<=") && strcmp(op, "&") && strcmp(op, "!&")) {
    SetError("operator must be == != >= <= & !&", line);
    return false;
  }
  if (!ParseUnsigned(tokens[2], &value, 16) || value > 0xFFFFul) {
    SetError("expected 16-bit hex comparison value", line);
    return false;
  }
  step->address = (uint16_t)address;
  step->value = (uint16_t)value;
  step->value_mask = 0xFFFF;
  snprintf(step->op, sizeof step->op, "%s", op);
  step->count = kDkc1ScriptDefaultTimeout;
  for (int i = 3; i + 1 < token_count + 1 && i < token_count; i += 2) {
    unsigned long extra;
    if (i + 1 >= token_count) {
      SetError("dangling option", line);
      return false;
    }
    if (strcmp(tokens[i], "mask") == 0 &&
        ParseUnsigned(tokens[i + 1], &extra, 16) && extra <= 0xFFFFul) {
      step->value_mask = (uint16_t)extra;
    } else if (strcmp(tokens[i], "timeout") == 0 &&
               ParseUnsigned(tokens[i + 1], &extra, 10) && extra >= 1) {
      step->count = (long)extra;
    } else {
      SetError("options are: mask HEX, timeout N", line);
      return false;
    }
  }
  return true;
}

static bool AppendStep(const Dkc1ScriptStep *step, long line) {
  if (s_step_count >= kDkc1ScriptMaxSteps) {
    SetError("script has too many steps", line);
    return false;
  }
  s_steps[s_step_count++] = *step;
  return true;
}

bool Dkc1ScriptLoad(const char *path, char *error, size_t error_size) {
  Dkc1ScriptFree();
  s_error[0] = '\0';
  FILE *stream = fopen(path, "r");
  if (!stream) {
    SetError("unable to open script", 0);
    if (error) snprintf(error, error_size, "%s", s_error);
    return false;
  }

  char line_text[512];
  long line = 0;
  bool ok = true;
  while (ok && fgets(line_text, sizeof line_text, stream)) {
    line++;
    char *comment = strpbrk(line_text, "#;");
    if (comment) *comment = '\0';
    char *text = Trim(line_text);
    if (!*text) continue;

    char *tokens[10];
    int token_count = 0;
    for (char *cursor = strtok(text, " \t"); cursor && token_count < 10;
         cursor = strtok(NULL, " \t"))
      tokens[token_count++] = cursor;
    if (!token_count) continue;

    Dkc1ScriptStep step;
    memset(&step, 0, sizeof step);
    step.line = line;

    if (strcmp(tokens[0], "wait") == 0) {
      step.kind = kOpWait;
      step.input_mask = 0;
      ok = ParsePredicate(tokens + 1, token_count - 1, &step, line) &&
           AppendStep(&step, line);
    } else if (strcmp(tokens[0], "hold") == 0) {
      unsigned long mask;
      if (token_count < 5 || !ParseUnsigned(tokens[1], &mask, 16)) {
        SetError("hold needs MASK then a predicate", line);
        ok = false;
      } else {
        step.kind = kOpWait;
        step.input_mask = (uint32_t)mask;
        ok = ParsePredicate(tokens + 2, token_count - 2, &step, line) &&
             AppendStep(&step, line);
      }
    } else if (strcmp(tokens[0], "checkpoint") == 0 ||
               strcmp(tokens[0], "state_save") == 0 ||
               strcmp(tokens[0], "state_load") == 0) {
      if (token_count != 2) {
        SetError("directive needs exactly one argument", line);
        ok = false;
      } else {
        step.kind = strcmp(tokens[0], "checkpoint") == 0 ? kOpCheckpoint
                    : strcmp(tokens[0], "state_save") == 0 ? kOpStateSave
                                                           : kOpStateLoad;
        step.text = _strdup(tokens[1]);
        ok = step.text && AppendStep(&step, line);
      }
    } else {
      /* MASK [* COUNT] — input_playback compatibility */
      unsigned long mask;
      if (!ParseUnsigned(tokens[0], &mask, 16) || mask > 0xFFFFFFul) {
        SetError("expected hex input mask or directive", line);
        ok = false;
      } else {
        unsigned long repeat = 1;
        int index = 1;
        if (index < token_count && strcmp(tokens[index], "*") == 0) index++;
        if (index < token_count &&
            (!ParseUnsigned(tokens[index], &repeat, 10) || repeat < 1 ||
             repeat > 1000000ul)) {
          SetError("repeat count must be 1..1000000", line);
          ok = false;
        }
        if (ok) {
          step.kind = kOpInput;
          step.input_mask = (uint32_t)mask;
          step.count = (long)repeat;
          ok = AppendStep(&step, line);
        }
      }
    }
  }
  const bool read_error = ferror(stream) != 0;
  fclose(stream);
  if (read_error) {
    SetError("unable to read script", 0);
    ok = false;
  }
  if (!ok) {
    if (error) snprintf(error, error_size, "%s", s_error);
    Dkc1ScriptFree();
    return false;
  }
  s_active = s_step_count > 0;
  return true;
}

bool Dkc1ScriptActive(void) {
  return s_active;
}

bool Dkc1ScriptFinished(void) {
  return !s_active || s_cursor >= s_step_count;
}

static bool PredicatePasses(const Dkc1ScriptStep *step, const uint8_t *wram) {
  const uint16_t raw = (uint16_t)(wram[step->address] |
                                  ((uint16_t)wram[step->address + 1] << 8));
  const uint16_t value = (uint16_t)(raw & step->value_mask);
  if (strcmp(step->op, "==") == 0) return value == step->value;
  if (strcmp(step->op, "!=") == 0) return value != step->value;
  if (strcmp(step->op, ">=") == 0) return value >= step->value;
  if (strcmp(step->op, "<=") == 0) return value <= step->value;
  if (strcmp(step->op, "&") == 0) return (value & step->value) != 0;
  return (value & step->value) == 0;  /* !& */
}

uint32_t Dkc1ScriptNextInput(const uint8_t *wram, Dkc1ScriptOps *ops,
                             bool *failed) {
  if (ops) memset(ops, 0, sizeof *ops);
  if (failed) *failed = false;
  if (!s_active || s_failed)
    return 0;

  /* Zero-frame directives execute immediately, attached to this frame. */
  while (s_cursor < s_step_count) {
    Dkc1ScriptStep *step = &s_steps[s_cursor];
    if (step->kind == kOpCheckpoint) {
      if (ops) ops->checkpoint = step->text;
      s_cursor++;
    } else if (step->kind == kOpStateSave) {
      if (ops) ops->state_save = step->text;
      s_cursor++;
    } else if (step->kind == kOpStateLoad) {
      if (ops) ops->state_load = step->text;
      s_cursor++;
    } else {
      break;
    }
  }
  if (s_cursor >= s_step_count)
    return 0;

  Dkc1ScriptStep *step = &s_steps[s_cursor];
  if (step->kind == kOpInput) {
    uint32_t mask = step->input_mask;
    if (++s_step_progress >= step->count) {
      s_cursor++;
      s_step_progress = 0;
    }
    return mask;
  }

  /* kOpWait */
  if (PredicatePasses(step, wram)) {
    s_cursor++;
    s_step_progress = 0;
    /* Re-evaluate from the new step this same frame so a satisfied wait
     * does not consume an extra neutral frame. */
    return Dkc1ScriptNextInput(wram, ops, failed);
  }
  if (++s_step_progress >= step->count) {
    s_failed = true;
    char message[128];
    snprintf(message, sizeof message,
             "wait timed out: [%04X]&%04X %s %04X after %ld frames",
             step->address, step->value_mask, step->op, step->value,
             step->count);
    SetError(message, step->line);
    if (failed) *failed = true;
    return 0;
  }
  return step->input_mask;
}

void Dkc1ScriptFree(void) {
  for (size_t i = 0; i < s_step_count; i++)
    free(s_steps[i].text);
  memset(s_steps, 0, sizeof s_steps);
  s_step_count = 0;
  s_cursor = 0;
  s_step_progress = 0;
  s_active = false;
  s_failed = false;
}
