
#define _GNU_SOURCE
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <regex.h>
#include <stdbool.h>

#include "Prototypes.h"
//#needs Script
//#needs PatternMatcher
//#needs Object
//#needs Text

/*{

typedef enum HighlightParserState_  {
   HPS_START,
   HPS_READ_FILES,
   HPS_SKIP_FILES,
   HPS_READ_RULES,
   HPS_ERROR
} HighlightParserState;

struct HighlightContextClass_ {
   ObjectClass super;
};

typedef enum {
   CLOSE_TOKEN,
   CLOSE_EOL,
   CLOSE_BLANKLINE,
} HighlightCloseMode;

struct HighlightContext_ {
   Object super;
   PatternMatcher* follows;
   HighlightContext* parent;
   HighlightContext* next;
   HighlightCloseMode mode;
   PatternMatcher* rules;
   int id;
   Color defaultColor;
};

typedef struct HighlightClose_ {
   HighlightCloseMode mode;
   union {
      char* token;
   };
} HighlightClose;

extern HighlightContextClass HighlightContextType;

struct Highlight_ {
   Vector* contexts;
   HighlightContext* mainContext;
   HighlightContext* currentContext;
   bool toLower;
   ScriptState* script;
   bool hasScript;
};

struct ReadHighlightFileArgs_ {
   Highlight* this;
   const char* fileName;
   Text firstLine;
   HighlightContext* context;
   Stack* contexts;
};

struct MatchArgs_ {
   const char* buffer;
   int* attrs;
   HighlightContext* ctx;
   Method_PatternMatcher_match match;
   int attrsAt;
   int y;
   bool paintUnmatched;
   bool lineStart;
};

#ifndef isword
#define isword(x) (isalpha(x) || x == '_')
#endif

}*/

HighlightContextClass HighlightContextType = {
   .super = {
      .size = sizeof(HighlightContext),
      .display = NULL,
      .equals = Object_equals,
      .delete = HighlightContext_delete
   }
};

Color Highlight_translateColorKey(char c) {
   switch (c) {
   case 'B': return BrightColor;
   case 'y': return SymbolColor;
   case 'Y': return BrightSymbolColor;
   case 'a': return AltColor;
   case 'A': return BrightAltColor;
   case 'd': return DiffColor;
   case 'D': return BrightDiffColor;
   case 's': return SpecialColor;
   case 'S': return BrightSpecialColor;
   case 'p': return SpecialDiffColor;
   case 'P': return BrightSpecialDiffColor;
   case '*': return VerySpecialColor;
   case '.': return DimColor;
   default: return NormalColor;
   }
}

static Color Highlight_translateColor(char* color) {
   if (String_eq(color, "bright")) return BrightColor;
   if (String_eq(color, "symbol")) return SymbolColor;
   if (String_eq(color, "brightsymbol")) return BrightSymbolColor;
   if (String_eq(color, "alt")) return AltColor;
   if (String_eq(color, "brightalt")) return BrightAltColor;
   if (String_eq(color, "diff")) return DiffColor;
   if (String_eq(color, "brightdiff")) return BrightDiffColor;
   if (String_eq(color, "special")) return SpecialColor;
   if (String_eq(color, "brightspecial")) return BrightSpecialColor;
   if (String_eq(color, "specialdiff")) return SpecialDiffColor;
   if (String_eq(color, "brightspecialdiff")) return BrightSpecialDiffColor;
   if (String_eq(color, "veryspecial")) return VerySpecialColor;
   if (String_eq(color, "dim")) return DimColor;
   return NormalColor;
}

Highlight* Highlight_new(const char* fileName, Text firstLine, ScriptState* script) {
   Highlight* this = (Highlight*) calloc(1, sizeof(Highlight));
   this->script = script;
   this->hasScript = false;

   this->contexts = Vector_new(ClassAs(HighlightContext, Object), true, DEFAULT_SIZE);
   this->currentContext = NULL;
   
   ReadHighlightFileArgs args;
   args.this = this;
   args.fileName = fileName;
   args.firstLine = firstLine;
   Files_forEachInDir("highlight", (Method_Files_fileHandler)Highlight_readHighlightFile, &args);
   if (!this->currentContext) {
      this->mainContext = Highlight_addContext(this, NULL, (HighlightClose){ .mode = CLOSE_EOL }, NULL, NormalColor);
      this->currentContext = this->mainContext;
   }
   return this;
}

void Highlight_delete(Highlight* this) {
   Vector_delete(this->contexts);
   free(this);
}

static void freeTokens(char** tokens) {
   if (tokens) {
      for (int i = 0; tokens[i]; i++)
         free(tokens[i]);
      free(tokens);
   }
}

HighlightParserState parseFile(ReadHighlightFileArgs* args, FILE* file, const char* name, HighlightParserState state) {
   if (!file) {
      return HPS_ERROR;
   }
   Highlight* this = args->this;
   const char* fileName = args->fileName;
   Text firstLine = args->firstLine;
   int lineno = 0;
   char** tokens = NULL;
   while (state != HPS_ERROR && !feof(file)) {
      freeTokens(tokens);
      tokens = NULL;
      char buffer[4096];
      char* ok = fgets(buffer, 4095, file);
      if (!ok) break;
      buffer[4095] = '\0';
      lineno++;
      char* ch = strchr(buffer, '\n');
      if (ch) *ch = '\0';
      ch = buffer;
      while (*ch == ' ' || *ch == '\t') ch++;
      if (*ch == '\0') continue;

      tokens = String_split(buffer, ' ');
      
      int ntokens;
      for (ntokens = 0; tokens[ntokens]; ntokens++);
      
      if (ntokens == 0 || tokens[0][0] == '#') {
         continue;
      }

      if (String_eq(tokens[0], "include") && ntokens == 2) {
         state = parseFile(args, Files_open("r", "highlight/%s", tokens[1]), tokens[1], state);
         continue;
      }

      switch (state) {
      case HPS_START:
      {
         // Read FILES header
         if (!String_eq(tokens[0], "FILES") && ntokens == 1)
            state = HPS_ERROR;
         else
            state = HPS_READ_FILES;
         break;
      }
      case HPS_READ_FILES:
      {
         // Try to match FILES rule
         const char* subject = NULL;
         if (!fileName) {
            state = HPS_ERROR;
            break;
         }
         if (String_eq(tokens[0],"name")) {
            subject = fileName;
            char* lastSlash = strrchr(subject, '/');
            if (lastSlash)
               subject = lastSlash + 1;
         } else if (String_eq(tokens[0], "firstline"))
            subject = firstLine.data;
         else {
            state = HPS_ERROR;
            break;
         }
         if (String_eq(tokens[1], "prefix") && ntokens == 3) {
            if (String_startsWith(subject, tokens[2]))
               state = HPS_SKIP_FILES;
         } else if (String_eq(tokens[1], "suffix") && ntokens == 3) {
            if (String_endsWith(subject, tokens[2]))
               state = HPS_SKIP_FILES;
         } else if (String_eq(tokens[1], "regex") && ntokens == 3) {
            regex_t magic;
            regcomp(&magic, tokens[2], REG_EXTENDED | REG_NOSUB);
            if (regexec(&magic, subject, 0, NULL, 0) == 0)
               state = HPS_SKIP_FILES;
            regfree(&magic);
         } else {
            state = HPS_ERROR;
         }
         break;
      }
      case HPS_SKIP_FILES:
      {
         // FILES match succeeded. Skip over other FILES section,
         // waiting for RULES section
         if (String_eq(tokens[0], "RULES") && ntokens == 1) {
            state = HPS_READ_RULES;
         }
         break;
      }
      case HPS_READ_RULES:
      {
         // Read RULES section
         if (String_eq(tokens[0], "context") && (ntokens == 4 || ntokens == 6)) {
            char* open = tokens[1];
            HighlightClose close;
            if (String_eq(tokens[2], "`$")) {
               close.mode = CLOSE_EOL;
            } else if (String_eq(tokens[2], "`^`$")) {
               close.mode = CLOSE_BLANKLINE;
            } else {
               close.mode = CLOSE_TOKEN;
               close.token = tokens[2];
            }
            Color color;
            if (ntokens == 6) {
               HighlightContext_addRule(args->context, open, Highlight_translateColor(tokens[3]), false, false);
               color = Highlight_translateColor(tokens[5]);
            } else {
               color = Highlight_translateColor(tokens[3]);
               HighlightContext_addRule(args->context, open, color, false, false);
            }
            args->context = Highlight_addContext(this, open, close, Stack_peek(args->contexts), color);
            if (close.mode == CLOSE_TOKEN) {
               color = (ntokens == 6 ? Highlight_translateColor(tokens[4]) : color);
               HighlightContext_addRule(args->context, close.token, color, false, false);
            }
            Stack_push(args->contexts, args->context);
         } else if (String_eq(tokens[0], "/context") && ntokens == 1) {
            if (args->contexts->size > 1) {
               Stack_pop(args->contexts);
               args->context = Stack_peek(args->contexts);
            }
         } else if (String_eq(tokens[0], "rule") && ntokens == 3) {
            HighlightContext_addRule(args->context, tokens[1], Highlight_translateColor(tokens[2]), false, false);
         } else if (String_eq(tokens[0], "eager_rule") && ntokens == 3) {
            HighlightContext_addRule(args->context, tokens[1], Highlight_translateColor(tokens[2]), true, false);
         } else if (String_eq(tokens[0], "handover_rule") && ntokens == 3) {
            HighlightContext_addRule(args->context, tokens[1], Highlight_translateColor(tokens[2]), false, true);
         } else if (String_eq(tokens[0], "insensitive") && ntokens == 1) {
            this->toLower = true;
         } else if (String_eq(tokens[0], "script") && ntokens == 2) {
            this->hasScript = Script_load(this->script, tokens[1]);
         } else {
            Display_errorScreen(0,0,"Error reading %s: line %d: %s", name, lineno, buffer);
            state = HPS_ERROR;
         }
         break;
      }
      case HPS_ERROR:
      {
         assert(0);
      }
      }
   }
   freeTokens(tokens);
   fclose(file);
   Script_load(this->script, "dit/global.lua");
   return state;
}

bool Highlight_readHighlightFile(ReadHighlightFileArgs* args, char* name) {
   Highlight* this = args->this;
   
   if (!String_endsWith(name, ".dithl")) {
      return false;
   }

   this->toLower = false;
   this->mainContext = Highlight_addContext(this, NULL, (HighlightClose){ .mode = CLOSE_EOL }, NULL, NormalColor);
   
   args->context = this->mainContext;
   args->contexts = Stack_new(ClassAs(HighlightContext, Object), false);
   
   Stack_push(args->contexts, args->context);

   HighlightParserState state = parseFile(args, fopen(name, "r"), name, HPS_START);

   if (args->contexts->size != 1) {
      Display_errorScreen("Error reading %s: %d context%s still open", name, args->contexts->size - 1, args->contexts->size > 2 ? "s" : "");
      state = HPS_ERROR;
   }
   Stack_delete(args->contexts);
   if (state == HPS_READ_RULES) {
      this->currentContext = this->mainContext;
      return true;
   }
   Vector_prune(this->contexts);
   return false;
}

HighlightContext* Highlight_addContext(Highlight* this, char* open, HighlightClose close, HighlightContext* parent, Color color) {
   int id = Vector_size(this->contexts);
   HighlightContext* ctx = HighlightContext_new(id, color, parent, close.mode);
   Vector_add(this->contexts, ctx);
   if (open) {
      assert(parent);
      PatternMatcher_add(parent->follows, (unsigned char*) open, (intptr_t) ctx, false, false);
   }
   if (close.mode == CLOSE_TOKEN) {
      assert(parent);
      PatternMatcher_add(ctx->follows, (unsigned char*) close.token, (intptr_t) parent, false, false);
   } else if (close.mode == CLOSE_EOL || close.mode == CLOSE_BLANKLINE) {
      if (parent) {
         // When multiple contexts ending with `$ are nested,
         // they should jump out to the outermost context.
         while (parent && parent->next && parent->next != parent) {
            parent = parent->next;
         }
         ctx->next = parent;
      }
   }
   return ctx;
}

#define PAINT(_args, _here, _attr) do { _args->attrs[_args->attrsAt++] = _attr; _here = UTF8_forward(_here, 1); } while(0)

static inline void Highlight_tryMatch(Highlight* this, MatchArgs* args, bool paintUnmatched) {
   GraphNode* rules   = args->lineStart ? args->ctx->rules->lineStart   : args->ctx->rules->start;
   GraphNode* follows = args->lineStart ? args->ctx->follows->lineStart : args->ctx->follows->start;
   const char* here = args->buffer;
   intptr_t intColor;
   bool eager, handOver;
   int saveAttrsAt = args->attrsAt;
   intptr_t matchlen = args->match(rules, args->buffer, &intColor, &eager, &handOver);
   Color color = (Color) intColor;
   assert(color >= 0 && color < Colors);
   if (matchlen && (eager || ( !(isword(here[matchlen-1]) && isword(here[matchlen]))))) {
      int attr = CRT_colors[color];
      const char* nextStop = here + matchlen;
      while (here < nextStop) {
         PAINT(args, here, attr);
      }
      if (handOver) {
         args->ctx = args->ctx->parent;
         args->attrsAt = saveAttrsAt;
         Highlight_tryMatch(this, args, paintUnmatched);
         return;
      }
      intptr_t nextCtx = 0;
      int followMatchlen = args->match(follows, args->buffer, &nextCtx, &eager, &handOver);
      if (followMatchlen == matchlen) {
         assert(nextCtx);
         args->ctx = (HighlightContext*) nextCtx;
      }
   } else if (paintUnmatched) {
      int attr = CRT_colors[args->ctx->defaultColor];
      if (*here) {
         if (isword(*here)) {
            do {
               PAINT(args, here, attr);
            } while (isword(*here));
         } else {
            PAINT(args, here, attr);
         }
      } else {
         args->attrs[args->attrsAt++] = attr;
         here++;
      }
   }
   args->buffer = here;
}

static void Highlight_lineStart(Highlight* this, MatchArgs* args) {
   HighlightContext* curCtx = args->ctx;
   if (curCtx->rules->lineStart) {
      if (!curCtx->follows->lineStart)
         curCtx->follows->lineStart = GraphNode_new();
      args->lineStart = true;
      Highlight_tryMatch(this, args, false);
      args->lineStart = false;
   }
}

void Highlight_setAttrs(Highlight* this, const char* buffer, int* attrs, int len, int y) {
   MatchArgs args = {
      .buffer = buffer,
      .attrs = attrs,
      .attrsAt = 0,
      .ctx = this->currentContext,
      .y = y,
      .match = (this->toLower) ? PatternMatcher_match_toLower : PatternMatcher_match,
   };
   Highlight_lineStart(this, &args);
   while (args.buffer < buffer + len) {
      Highlight_tryMatch(this, &args, true);
   }
   Script_highlightLine(this, buffer, attrs, len, y);
   this->currentContext = (args.ctx->mode == CLOSE_BLANKLINE && len != 0)
                        ? args.ctx
                        : args.ctx->next;
}

inline HighlightContext* Highlight_getContext(Highlight* this) {
   return this->currentContext;
}

inline void Highlight_setContext(Highlight* this, HighlightContext* context) {
   this->currentContext = (HighlightContext*) context;
}

HighlightContext* HighlightContext_new(int id, Color defaultColor, HighlightContext* parent, HighlightCloseMode mode) {
   HighlightContext* this = Alloc(HighlightContext);
   this->id = id;
   this->follows = PatternMatcher_new();
   this->defaultColor = defaultColor;
   this->rules = PatternMatcher_new();
   this->next = this;
   this->mode = mode;
   this->parent = parent;
   return this;
}

void HighlightContext_delete(Object* cast) {
   HighlightContext* this = (HighlightContext*) cast;
   PatternMatcher_delete(this->follows);
   PatternMatcher_delete(this->rules);
   free(this);
}

void HighlightContext_addRule(HighlightContext* this, char* rule, Color color, bool eager, bool handOver) {
   PatternMatcher_add(this->rules, (unsigned char*) rule, (int) color, eager, handOver);
}
