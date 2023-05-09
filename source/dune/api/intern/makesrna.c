#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_string.h"
#include "BLI_system.h" /* for 'BLI_system_backtrace' stub. */
#include "BLI_utildefines.h"

#include "RNA_define.h"
#include "RNA_enum_types.h"
#include "RNA_types.h"

#include "rna_internal.h"

#ifdef _WIN32
#  ifndef snprintf
#    define snprintf _snprintf
#  endif
#endif

#include "CLG_log.h"

static CLG_LogRef LOG = {"makesrna"};

/**
 * Variable to control debug output of makesrna.
 * debugSRNA:
 * - 0 = no output, except errors
 * - 1 = detail actions
 */
static int debugSRNA = 0;

/* stub for BLI_abort() */
#ifndef NDEBUG
void BLI_system_backtrace(FILE *fp)
{
  (void)fp;
}
#endif

/* Replace if different */
#define TMP_EXT ".tmp"

/* copied from BLI_file_older */
#include <sys/stat.h>
static int file_older(const char *file1, const char *file2)
{
  struct stat st1, st2;
  if (debugSRNA > 0) {
    printf("compare: %s %s\n", file1, file2);
  }

  if (stat(file1, &st1)) {
    return 0;
  }
  if (stat(file2, &st2)) {
    return 0;
  }

  return (st1.st_mtime < st2.st_mtime);
}
static const char *makesrna_path = NULL;

/* forward declarations */
static void rna_generate_static_parameter_prototypes(FILE *f,
                                                     StructRNA *srna,
                                                     FunctionDefRNA *dfunc,
                                                     const char *name_override,
                                                     int close_prototype);

/* helpers */
#define WRITE_COMMA \
  { \
    if (!first) { \
      fprintf(f, ", "); \
    } \
    first = 0; \
  } \
  (void)0

#define WRITE_PARAM(param) \
  { \
    WRITE_COMMA; \
    fprintf(f, param); \
  } \
  (void)0

static int replace_if_different(const char *tmpfile, const char *dep_files[])
{
  /* return 0; */ /* use for testing had edited rna */

#define REN_IF_DIFF \
  { \
    FILE *file_test = fopen(orgfile, "rb"); \
    if (file_test) { \
      fclose(file_test); \
      if (fp_org) { \
        fclose(fp_org); \
      } \
      if (fp_new) { \
        fclose(fp_new); \
      } \
      if (remove(orgfile) != 0) { \
        CLOG_ERROR(&LOG, "remove error (%s): \"%s\"", strerror(errno), orgfile); \
        return -1; \
      } \
    } \
  } \
  if (rename(tmpfile, orgfile) != 0) { \
    CLOG_ERROR(&LOG, "rename error (%s): \"%s\" -> \"%s\"", strerror(errno), tmpfile, orgfile); \
    return -1; \
  } \
  remove(tmpfile); \
  return 1

  /* end REN_IF_DIFF */

  FILE *fp_new = NULL, *fp_org = NULL;
  int len_new, len_org;
  char *arr_new, *arr_org;
  int cmp;

  char orgfile[4096];

  strcpy(orgfile, tmpfile);
  orgfile[strlen(orgfile) - strlen(TMP_EXT)] = '\0'; /* strip '.tmp' */

  fp_org = fopen(orgfile, "rb");

  if (fp_org == NULL) {
    REN_IF_DIFF;
  }

  /* XXX, trick to work around dependency problem
   * assumes dep_files is in the same dir as makesrna.c, which is true for now. */

  if (1) {
    /* first check if makesrna.c is newer than generated files
     * for development on makesrna.c you may want to disable this */
    if (file_older(orgfile, __FILE__)) {
      REN_IF_DIFF;
    }

    if (file_older(orgfile, makesrna_path)) {
      REN_IF_DIFF;
    }

    /* now check if any files we depend on are newer than any generated files */
    if (dep_files) {
      int pass;
      for (pass = 0; dep_files[pass]; pass++) {
        const char from_path[4096] = __FILE__;
        char *p1, *p2;

        /* dir only */
        p1 = strrchr(from_path, '/');
        p2 = strrchr(from_path, '\\');
        strcpy((p1 > p2 ? p1 : p2) + 1, dep_files[pass]);
        /* account for build deps, if makesrna.c (this file) is newer */
        if (file_older(orgfile, from_path)) {
          REN_IF_DIFF;
        }
      }
    }
  }
  /* XXX end dep trick */

  fp_new = fopen(tmpfile, "rb");

  if (fp_new == NULL) {
    /* shouldn't happen, just to be safe */
    CLOG_ERROR(&LOG, "open error: \"%s\"", tmpfile);
    fclose(fp_org);
    return -1;
  }

  fseek(fp_new, 0L, SEEK_END);
  len_new = ftell(fp_new);
  fseek(fp_new, 0L, SEEK_SET);
  fseek(fp_org, 0L, SEEK_END);
  len_org = ftell(fp_org);
  fseek(fp_org, 0L, SEEK_SET);

  if (len_new != len_org) {
    fclose(fp_new);
    fp_new = NULL;
    fclose(fp_org);
    fp_org = NULL;
    REN_IF_DIFF;
  }

  /* now compare the files... */
  arr_new = MEM_mallocN(sizeof(char) * len_new, "rna_cmp_file_new");
  arr_org = MEM_mallocN(sizeof(char) * len_org, "rna_cmp_file_org");

  if (fread(arr_new, sizeof(char), len_new, fp_new) != len_new) {
    CLOG_ERROR(&LOG, "unable to read file %s for comparison.", tmpfile);
  }
  if (fread(arr_org, sizeof(char), len_org, fp_org) != len_org) {
    CLOG_ERROR(&LOG, "unable to read file %s for comparison.", orgfile);
  }

  fclose(fp_new);
  fp_new = NULL;
  fclose(fp_org);
  fp_org = NULL;

  cmp = memcmp(arr_new, arr_org, len_new);

  MEM_freeN(arr_new);
  MEM_freeN(arr_org);

  if (cmp) {
    REN_IF_DIFF;
  }
  remove(tmpfile);
  return 0;

#undef REN_IF_DIFF
}

/* Helper to solve keyword problems with C/C++ */

static const char *rna_safe_id(const char *id)
{
  if (STREQ(id, "default")) {
    return "default_value";
  }
  if (STREQ(id, "operator")) {
    return "operator_value";
  }
  if (STREQ(id, "new")) {
    return "create";
  }
  if (STREQ(id, "co_return")) {
    /* MSVC2015, C++ uses for coroutines */
    return "coord_return";
  }

  return id;
}

/* Sorting */

static int cmp_struct(const void *a, const void *b)
{
  const StructRNA *structa = *(const StructRNA **)a;
  const StructRNA *structb = *(const StructRNA **)b;

  return strcmp(structa->identifier, structb->identifier);
}

static int cmp_property(const void *a, const void *b)
{
  const PropertyRNA *propa = *(const PropertyRNA **)a;
  const PropertyRNA *propb = *(const PropertyRNA **)b;

  if (STREQ(propa->identifier, "rna_type")) {
    return -1;
  }
  if (STREQ(propb->identifier, "rna_type")) {
    return 1;
  }

  if (STREQ(propa->identifier, "name")) {
    return -1;
  }
  if (STREQ(propb->identifier, "name")) {
    return 1;
  }

  return strcmp(propa->name, propb->name);
}

static int cmp_def_struct(const void *a, const void *b)
{
  const StructDefRNA *dsa = *(const StructDefRNA **)a;
  const StructDefRNA *dsb = *(const StructDefRNA **)b;

  return cmp_struct(&dsa->srna, &dsb->srna);
}

static int cmp_def_property(const void *a, const void *b)
{
  const PropertyDefRNA *dpa = *(const PropertyDefRNA **)a;
  const PropertyDefRNA *dpb = *(const PropertyDefRNA **)b;

  return cmp_property(&dpa->prop, &dpb->prop);
}

static void rna_sortlist(ListBase *listbase, int (*cmp)(const void *, const void *))
{
  Link *link;
  void **array;
  int a, size;

  if (listbase->first == listbase->last) {
    return;
  }

  for (size = 0, link = listbase->first; link; link = link->next) {
    size++;
  }

  array = MEM_mallocN(sizeof(void *) * size, "rna_sortlist");
  for (a = 0, link = listbase->first; link; link = link->next, a++) {
    array[a] = link;
  }

  qsort(array, size, sizeof(void *), cmp);

  listbase->first = listbase->last = NULL;
  for (a = 0; a < size; a++) {
    link = array[a];
    link->next = link->prev = NULL;
    rna_addtail(listbase, link);
  }

  MEM_freeN(array);
}

/* Preprocessing */

static void rna_print_c_string(FILE *f, const char *str)
{
  static const char *escape[] = {
      "\''", "\"\"", "\??", "\\\\", "\aa", "\bb", "\ff", "\nn", "\rr", "\tt", "\vv", NULL};
  int i, j;

  if (!str) {
    fprintf(f, "NULL");
    return;
  }

  fprintf(f, "\"");
  for (i = 0; str[i]; i++) {
    for (j = 0; escape[j]; j++) {
      if (str[i] == escape[j][0]) {
        break;
      }
    }

    if (escape[j]) {
      fprintf(f, "\\%c", escape[j][1]);
    }
    else {
      fprintf(f, "%c", str[i]);
    }
  }
  fprintf(f, "\"");
}

static void rna_print_data_get(FILE *f, PropertyDefRNA *dp)
{
  if (dp->dnastructfromname && dp->dnastructfromprop) {
    fprintf(f,
            "    %s *data = (%s *)(((%s *)ptr->data)->%s);\n",
            dp->dnastructname,
            dp->dnastructname,
            dp->dnastructfromname,
            dp->dnastructfromprop);
  }
  else {
    fprintf(f, "    %s *data = (%s *)(ptr->data);\n", dp->dnastructname, dp->dnastructname);
  }
}

static void rna_print_id_get(FILE *f, PropertyDefRNA *UNUSED(dp))
{
  fprintf(f, "    ID *id = ptr->owner_id;\n");
}

static void rna_construct_function_name(
    char *buffer, int size, const char *structname, const char *propname, const char *type)
{
  snprintf(buffer, size, "%s_%s_%s", structname, propname, type);
}

static void rna_construct_wrapper_function_name(
    char *buffer, int size, const char *structname, const char *propname, const char *type)
{
  if (type == NULL || type[0] == '\0') {
    snprintf(buffer, size, "%s_%s", structname, propname);
  }
  else {
    snprintf(buffer, size, "%s_%s_%s", structname, propname, type);
  }
}

void *rna_alloc_from_buffer(const char *buffer, int buffer_len)
{
  AllocDefRNA *alloc = MEM_callocN(sizeof(AllocDefRNA), "AllocDefRNA");
  alloc->mem = MEM_mallocN(buffer_len, __func__);
  memcpy(alloc->mem, buffer, buffer_len);
  rna_addtail(&DefRNA.allocs, alloc);
  return alloc->mem;
}

void *rna_calloc(int buffer_len)
{
  AllocDefRNA *alloc = MEM_callocN(sizeof(AllocDefRNA), "AllocDefRNA");
  alloc->mem = MEM_callocN(buffer_len, __func__);
  rna_addtail(&DefRNA.allocs, alloc);
  return alloc->mem;
}

static char *rna_alloc_function_name(const char *structname,
                                     const char *propname,
                                     const char *type)
{
  char buffer[2048];
  rna_construct_function_name(buffer, sizeof(buffer), structname, propname, type);
  return rna_alloc_from_buffer(buffer, strlen(buffer) + 1);
}

static StructRNA *rna_find_struct(const char *identifier)
{
  StructDefRNA *ds;

  for (ds = DefRNA.structs.first; ds; ds = ds->cont.next) {
    if (STREQ(ds->srna->identifier, identifier)) {
      return ds->srna;
    }
  }

  return NULL;
}

static const char *rna_find_type(const char *type)
{
  StructDefRNA *ds;

  for (ds = DefRNA.structs.first; ds; ds = ds->cont.next) {
    if (ds->dnaname && STREQ(ds->dnaname, type)) {
      return ds->srna->identifier;
    }
  }

  return NULL;
}

static const char *rna_find_dna_type(const char *type)
{
  StructDefRNA *ds;

  for (ds = DefRNA.structs.first; ds; ds = ds->cont.next) {
    if (STREQ(ds->srna->identifier, type)) {
      return ds->dnaname;
    }
  }

  return NULL;
}

static const char *rna_type_type_name(PropertyRNA *prop)
{
  switch (prop->type) {
    case PROP_BOOLEAN:
      return "bool";
    case PROP_INT:
      return "int";
    case PROP_ENUM: {
      EnumPropertyRNA *eprop = (EnumPropertyRNA *)prop;
      if (eprop->native_enum_type) {
        return eprop->native_enum_type;
      }
      return "int";
    }
    case PROP_FLOAT:
      return "float";
    case PROP_STRING:
      if (prop->flag & PROP_THICK_WRAP) {
        return "char *";
      }
      else {
        return "const char *";
      }
    default:
      return NULL;
  }
}

static const char *rna_type_type(PropertyRNA *prop)
{
  const char *type;

  type = rna_type_type_name(prop);

  if (type) {
    return type;
  }

  return "PointerRNA";
}

static const char *rna_type_struct(PropertyRNA *prop)
{
  const char *type;

  type = rna_type_type_name(prop);

  if (type) {
    return "";
  }

  return "struct ";
}

static const char *rna_parameter_type_name(PropertyRNA *parm)
{
  const char *type;

  type = rna_type_type_name(parm);

  if (type) {
    return type;
  }

  switch (parm->type) {
    case PROP_POINTER: {
      PointerPropertyRNA *pparm = (PointerPropertyRNA *)parm;

      if (parm->flag_parameter & PARM_RNAPTR) {
        return "PointerRNA";
      }
      return rna_find_dna_type((const char *)pparm->type);
    }
    case PROP_COLLECTION: {
      return "CollectionListBase";
    }
    default:
      return "<error, no type specified>";
  }
}

static int rna_enum_bitmask(PropertyRNA *prop)
{
  EnumPropertyRNA *eprop = (EnumPropertyRNA *)prop;
  int a, mask = 0;

  if (eprop->item) {
    for (a = 0; a < eprop->totitem; a++) {
      if (eprop->item[a].identifier[0]) {
        mask |= eprop->item[a].value;
      }
    }
  }

  return mask;
}

static int rna_color_quantize(PropertyRNA *prop, PropertyDefRNA *dp)
{
  return ((prop->type == PROP_FLOAT) && (ELEM(prop->subtype, PROP_COLOR, PROP_COLOR_GAMMA)) &&
          (IS_DNATYPE_FLOAT_COMPAT(dp->dnatype) == 0));
}

/**
 * Return the identifier for an enum which is defined in "RNA_enum_items.h".
 *
 * Prevents expanding duplicate enums bloating the binary size.
 */
static const char *rna_enum_id_from_pointer(const EnumPropertyItem *item)
{
#define RNA_MAKESRNA
#define DEF_ENUM(id) \
  if (item == id) { \
    return STRINGIFY(id); \
  }
#include "RNA_enum_items.h"
#undef RNA_MAKESRNA
  return NULL;
}

static const char *rna_function_string(const void *func)
{
  return (func) ? (const char *)func : "NULL";
}

static void rna_float_print(FILE *f, float num)
{
  if (num == -FLT_MAX) {
    fprintf(f, "-FLT_MAX");
  }
  else if (num == FLT_MAX) {
    fprintf(f, "FLT_MAX");
  }
  else if ((fabsf(num) < (float)INT64_MAX) && ((int64_t)num == num)) {
    fprintf(f, "%.1ff", num);
  }
  else {
    fprintf(f, "%.10ff", num);
  }
}

static void rna_int_print(FILE *f, int64_t num)
{
  if (num == INT_MIN) {
    fprintf(f, "INT_MIN");
  }
  else if (num == INT_MAX) {
    fprintf(f, "INT_MAX");
  }
  else if (num == INT64_MIN) {
    fprintf(f, "INT64_MIN");
  }
  else if (num == INT64_MAX) {
    fprintf(f, "INT64_MAX");
  }
  else if (num < INT_MIN || num > INT_MAX) {
    fprintf(f, "%" PRId64 "LL", num);
  }
  else {
    fprintf(f, "%d", (int)num);
  }
}

static char *rna_def_property_get_func(
    FILE *f, StructRNA *srna, PropertyRNA *prop, PropertyDefRNA *dp, const char *manualfunc)
{
  char *func;

  if (prop->flag & PROP_IDPROPERTY && manualfunc == NULL) {
    return NULL;
  }

  if (!manualfunc) {
    if (!dp->dnastructname || !dp->dnaname) {
      CLOG_ERROR(&LOG, "%s.%s has no valid dna info.", srna->identifier, prop->identifier);
      DefRNA.error = true;
      return NULL;
    }

    /* Type check. */
    if (dp->dnatype && *dp->dnatype) {

      if (prop->type == PROP_FLOAT) {
        if (IS_DNATYPE_FLOAT_COMPAT(dp->dnatype) == 0) {
          /* Colors are an exception. these get translated. */
          if (prop->subtype != PROP_COLOR_GAMMA) {
            CLOG_ERROR(&LOG,
                       "%s.%s is a '%s' but wrapped as type '%s'.",
                       srna->identifier,
                       prop->identifier,
                       dp->dnatype,
                       RNA_property_typename(prop->type));
            DefRNA.error = true;
            return NULL;
          }
        }
      }
      else if (prop->type == PROP_BOOLEAN) {
        if (IS_DNATYPE_BOOLEAN_COMPAT(dp->dnatype) == 0) {
          CLOG_ERROR(&LOG,
                     "%s.%s is a '%s' but wrapped as type '%s'.",
                     srna->identifier,
                     prop->identifier,
                     dp->dnatype,
                     RNA_property_typename(prop->type));
          DefRNA.error = true;
          return NULL;
        }
      }
      else if (ELEM(prop->type, PROP_INT, PROP_ENUM)) {
        if (IS_DNATYPE_INT_COMPAT(dp->dnatype) == 0) {
          CLOG_ERROR(&LOG,
                     "%s.%s is a '%s' but wrapped as type '%s'.",
                     srna->identifier,
                     prop->identifier,
                     dp->dnatype,
                     RNA_property_typename(prop->type));
          DefRNA.error = true;
          return NULL;
        }
      }
    }

    /* Check log scale sliders for negative range. */
    if (prop->type == PROP_FLOAT) {
      FloatPropertyRNA *fprop = (FloatPropertyRNA *)prop;
      /* NOTE: UI_BTYPE_NUM_SLIDER can't have a softmin of zero. */
      if ((fprop->ui_scale_type == PROP_SCALE_LOG) && (fprop->hardmin < 0 || fprop->softmin < 0)) {
        CLOG_ERROR(
            &LOG, "\"%s.%s\", range for log scale < 0.", srna->identifier, prop->identifier);
        DefRNA.error = true;
        return NULL;
      }
    }
    if (prop->type == PROP_INT) {
      IntPropertyRNA *iprop = (IntPropertyRNA *)prop;
      /* Only UI_BTYPE_NUM_SLIDER is implemented and that one can't have a softmin of zero. */
      if ((iprop->ui_scale_type == PROP_SCALE_LOG) &&
          (iprop->hardmin <= 0 || iprop->softmin <= 0)) {
        CLOG_ERROR(
            &LOG, "\"%s.%s\", range for log scale <= 0.", srna->identifier, prop->identifier);
        DefRNA.error = true;
        return NULL;
      }
    }
  }

  func = rna_alloc_function_name(srna->identifier, rna_safe_id(prop->identifier), "get");

  switch (prop->type) {
    case PROP_STRING: {
      StringPropertyRNA *sprop = (StringPropertyRNA *)prop;
      fprintf(f, "void %s(PointerRNA *ptr, char *value)\n", func);
      fprintf(f, "{\n");
      if (manualfunc) {
        fprintf(f, "    %s(ptr, value);\n", manualfunc);
      }
      else {
        const PropertySubType subtype = prop->subtype;
        const char *string_copy_func =
            ELEM(subtype, PROP_FILEPATH, PROP_DIRPATH, PROP_FILENAME, PROP_BYTESTRING) ?
                "BLI_strncpy" :
                "BLI_strncpy_utf8";

        rna_print_data_get(f, dp);

        if (dp->dnapointerlevel == 1) {
          /* Handle allocated char pointer properties. */
          fprintf(f, "    if (data->%s == NULL) {\n", dp->dnaname);
          fprintf(f, "        *value = '\\0';\n");
          fprintf(f, "        return;\n");
          fprintf(f, "    }\n");
          fprintf(f,
                  "    %s(value, data->%s, strlen(data->%s) + 1);\n",
                  string_copy_func,
                  dp->dnaname,
                  dp->dnaname);
        }
        else {
          /* Handle char array properties. */
          if (sprop->maxlength) {
            fprintf(f,
                    "    %s(value, data->%s, %d);\n",
                    string_copy_func,
                    dp->dnaname,
                    sprop->maxlength);
          }
          else {
            fprintf(f,
                    "    %s(value, data->%s, sizeof(data->%s));\n",
                    string_copy_func,
                    dp->dnaname,
                    dp->dnaname);
          }
        }
      }
      fprintf(f, "}\n\n");
      break;
    }
    case PROP_POINTER: {
      fprintf(f, "PointerRNA %s(PointerRNA *ptr)\n", func);
      fprintf(f, "{\n");
      if (manualfunc) {
        fprintf(f, "    return %s(ptr);\n", manualfunc);
      }
      else {
        PointerPropertyRNA *pprop = (PointerPropertyRNA *)prop;
        rna_print_data_get(f, dp);
        if (dp->dnapointerlevel == 0) {
          fprintf(f,
                  "    return rna_pointer_inherit_refine(ptr, &RNA_%s, &data->%s);\n",
                  (const char *)pprop->type,
                  dp->dnaname);
        }
        else {
          fprintf(f,
                  "    return rna_pointer_inherit_refine(ptr, &RNA_%s, data->%s);\n",
                  (const char *)pprop->type,
                  dp->dnaname);
        }
      }
      fprintf(f, "}\n\n");
      break;
    }
    case PROP_COLLECTION: {
      CollectionPropertyRNA *cprop = (CollectionPropertyRNA *)prop;

      fprintf(f, "static PointerRNA %s(CollectionPropertyIterator *iter)\n", func);
      fprintf(f, "{\n");
      if (manualfunc) {
        if (STR_ELEM(manualfunc,
                     "rna_iterator_listbase_get",
                     "rna_iterator_array_get",
                     "rna_iterator_array_dereference_get")) {
          fprintf(f,
                  "    return rna_pointer_inherit_refine(&iter->parent, &RNA_%s, %s(iter));\n",
                  (cprop->item_type) ? (const char *)cprop->item_type : "UnknownType",
                  manualfunc);
        }
        else {
          fprintf(f, "    return %s(iter);\n", manualfunc);
        }
      }
      fprintf(f, "}\n\n");
      break;
    }
    default:
      if (prop->arraydimension) {
        if (prop->flag & PROP_DYNAMIC) {
          fprintf(f, "void %s(PointerRNA *ptr, %s values[])\n", func, rna_type_type(prop));
        }
        else {
          fprintf(f,
                  "void %s(PointerRNA *ptr, %s values[%u])\n",
                  func,
                  rna_type_type(prop),
                  prop->totarraylength);
        }
        fprintf(f, "{\n");

        if (manualfunc) {
          fprintf(f, "    %s(ptr, values);\n", manualfunc);
        }
        else {
          rna_print_data_get(f, dp);

          if (prop->flag & PROP_DYNAMIC) {
            char *lenfunc = rna_alloc_function_name(
                srna->identifier, rna_safe_id(prop->identifier), "get_length");
            fprintf(f, "    unsigned int arraylen[RNA_MAX_ARRAY_DIMENSION];\n");
            fprintf(f, "    unsigned int i;\n");
            fprintf(f, "    unsigned int len = %s(ptr, arraylen);\n\n", lenfunc);
            fprintf(f, "    for (i = 0; i < len; i++) {\n");
            MEM_freeN(lenfunc);
          }
          else {
            fprintf(f, "    unsigned int i;\n\n");
            fprintf(f, "    for (i = 0; i < %u; i++) {\n", prop->totarraylength);
          }

          if (dp->dnaarraylength == 1) {
            if (prop->type == PROP_BOOLEAN && dp->booleanbit) {
              fprintf(f,
                      "        values[i] = %s((data->%s & (",
                      (dp->booleannegative) ? "!" : "",
                      dp->dnaname);
              rna_int_print(f, dp->booleanbit);
              fprintf(f, " << i)) != 0);\n");
            }
            else {
              fprintf(f,
                      "        values[i] = (%s)%s((&data->%s)[i]);\n",
                      rna_type_type(prop),
                      (dp->booleannegative) ? "!" : "",
                      dp->dnaname);
            }
          }
          else {
            if (prop->type == PROP_BOOLEAN && dp->booleanbit) {
              fprintf(f,
                      "        values[i] = %s((data->%s[i] & ",
                      (dp->booleannegative) ? "!" : "",
                      dp->dnaname);
              rna_int_print(f, dp->booleanbit);
              fprintf(f, ") != 0);\n");
            }
            else if (rna_color_quantize(prop, dp)) {
              fprintf(f,
                      "        values[i] = (%s)(data->%s[i] * (1.0f / 255.0f));\n",
                      rna_type_type(prop),
                      dp->dnaname);
            }
            else if (dp->dnatype) {
              fprintf(f,
                      "        values[i] = (%s)%s(((%s *)data->%s)[i]);\n",
                      rna_type_type(prop),
                      (dp->booleannegative) ? "!" : "",
                      dp->dnatype,
                      dp->dnaname);
            }
            else {
              fprintf(f,
                      "        values[i] = (%s)%s((data->%s)[i]);\n",
                      rna_type_type(prop),
                      (dp->booleannegative) ? "!" : "",
                      dp->dnaname);
            }
          }
          fprintf(f, "    }\n");
        }
        fprintf(f, "}\n\n");
      }
      else {
        fprintf(f, "%s %s(PointerRNA *ptr)\n", rna_type_type(prop), func);
        fprintf(f, "{\n");

        if (manualfunc) {
          fprintf(f, "    return %s(ptr);\n", manualfunc);
        }
        else {
          rna_print_data_get(f, dp);
          if (prop->type == PROP_BOOLEAN && dp->booleanbit) {
            fprintf(
                f, "    return %s(((data->%s) & ", (dp->booleannegative) ? "!" : "", dp->dnaname);
            rna_int_print(f, dp->booleanbit);
            fprintf(f, ") != 0);\n");
          }
          else if (prop->type == PROP_ENUM && dp->enumbitflags) {
            fprintf(f, "    return ((data->%s) & ", dp->dnaname);
            rna_int_print(f, rna_enum_bitmask(prop));
            fprintf(f, ");\n");
          }
          else {
            fprintf(f,
                    "    return (%s)%s(data->%s);\n",
                    rna_type_type(prop),
                    (dp->booleannegative) ? "!" : "",
                    dp->dnaname);
          }
        }

        fprintf(f, "}\n\n");
      }
      break;
  }

  return func;
}

/* defined min/max variables to be used by rna_clamp_value() */
static void rna_clamp_value_range(FILE *f, PropertyRNA *prop)
{
  if (prop->type == PROP_FLOAT) {
    FloatPropertyRNA *fprop = (FloatPropertyRNA *)prop;
    if (fprop->range) {
      fprintf(f,
              "    float prop_clamp_min = -FLT_MAX, prop_clamp_max = FLT_MAX, prop_soft_min, "
              "prop_soft_max;\n");
      fprintf(f,
              "    %s(ptr, &prop_clamp_min, &prop_clamp_max, &prop_soft_min, &prop_soft_max);\n",
              rna_function_string(fprop->range));
    }
  }
  else if (prop->type == PROP_INT) {
    IntPropertyRNA *iprop = (IntPropertyRNA *)prop;
    if (iprop->range) {
      fprintf(f,
              "    int prop_clamp_min = INT_MIN, prop_clamp_max = INT_MAX, prop_soft_min, "
              "prop_soft_max;\n");
      fprintf(f,
              "    %s(ptr, &prop_clamp_min, &prop_clamp_max, &prop_soft_min, &prop_soft_max);\n",
              rna_function_string(iprop->range));
    }
  }
}

#ifdef USE_RNA_RANGE_CHECK
static void rna_clamp_value_range_check(FILE *f,
                                        PropertyRNA *prop,
                                        const char *dnaname_prefix,
                                        const char *dnaname)
{
  if (prop->type == PROP_INT) {
    IntPropertyRNA *iprop = (IntPropertyRNA *)prop;
    fprintf(f,
            "    { BLI_STATIC_ASSERT("
            "(TYPEOF_MAX(%s%s) >= %d) && "
            "(TYPEOF_MIN(%s%s) <= %d), "
            "\"invalid limits\"); }\n",
            dnaname_prefix,
            dnaname,
            iprop->hardmax,
            dnaname_prefix,
            dnaname,
            iprop->hardmin);
  }
}
#endif /* USE_RNA_RANGE_CHECK */

static void rna_clamp_value(FILE *f, PropertyRNA *prop, int array)
{
  if (prop->type == PROP_INT) {
    IntPropertyRNA *iprop = (IntPropertyRNA *)prop;

    if (iprop->hardmin != INT_MIN || iprop->hardmax != INT_MAX || iprop->range) {
      if (array) {
        fprintf(f, "CLAMPIS(values[i], ");
      }
      else {
        fprintf(f, "CLAMPIS(value, ");
      }
      if (iprop->range) {
        fprintf(f, "prop_clamp_min, prop_clamp_max);");
      }
      else {
        rna_int_print(f, iprop->hardmin);
        fprintf(f, ", ");
        rna_int_print(f, iprop->hardmax);
        fprintf(f, ");\n");
      }
      return;
    }
  }
  else if (prop->type == PROP_FLOAT) {
    FloatPropertyRNA *fprop = (FloatPropertyRNA *)prop;

    if (fprop->hardmin != -FLT_MAX || fprop->hardmax != FLT_MAX || fprop->range) {
      if (array) {
        fprintf(f, "CLAMPIS(values[i], ");
      }
      else {
        fprintf(f, "CLAMPIS(value, ");
      }
      if (fprop->range) {
        fprintf(f, "prop_clamp_min, prop_clamp_max);");
      }
      else {
        rna_float_print(f, fprop->hardmin);
        fprintf(f, ", ");
        rna_float_print(f, fprop->hardmax);
        fprintf(f, ");\n");
      }
      return;
    }
  }

  if (array) {
    fprintf(f, "values[i];\n");
  }
  else {
    fprintf(f, "value;\n");
  }
}

static char *rna_def_property_set_func(
    FILE *f, StructRNA *srna, PropertyRNA *prop, PropertyDefRNA *dp, const char *manualfunc)
{
  char *func;

  if (!(prop->flag & PROP_EDITABLE)) {
    return NULL;
  }
  if (prop->flag & PROP_IDPROPERTY && manualfunc == NULL) {
    return NULL;
  }

  if (!manualfunc) {
    if (!dp->dnastructname || !dp->dnaname) {
      if (prop->flag & PROP_EDITABLE) {
        CLOG_ERROR(&LOG, "%s.%s has no valid dna info.", srna->identifier, prop->identifier);
        DefRNA.error = true;
      }
      return NULL;
    }
  }

  func = rna_alloc_function_name(srna->identifier, rna_safe_id(prop->identifier), "set");

  switch (prop->type) {
    case PROP_STRING: {
      StringPropertyRNA *sprop = (StringPropertyRNA *)prop;
      fprintf(f, "void %s(PointerRNA *ptr, const char *value)\n", func);
      fprintf(f, "{\n");
      if (manualfunc) {
        fprintf(f, "    %s(ptr, value);\n", manualfunc);
      }
      else {
        const PropertySubType subtype = prop->subtype;
        const char *string_copy_func =
            ELEM(subtype, PROP_FILEPATH, PROP_DIRPATH, PROP_FILENAME, PROP_BYTESTRING) ?
                "BLI_strncpy" :
                "BLI_strncpy_utf8";

        rna_print_data_get(f, dp);

        if (dp->dnapointerlevel == 1) {
          /* Handle allocated char pointer properties. */
          fprintf(
              f, "    if (data->%s != NULL) { MEM_freeN(data->%s); }\n", dp->dnaname, dp->dnaname);
          fprintf(f, "    const int length = strlen(value);\n");
          fprintf(f, "    data->%s = MEM_mallocN(length + 1, __func__);\n", dp->dnaname);
          fprintf(f, "    %s(data->%s, value, length + 1);\n", string_copy_func, dp->dnaname);
        }
        else {
          /* Handle char array properties. */
          if (sprop->maxlength) {
            fprintf(f,
                    "    %s(data->%s, value, %d);\n",
                    string_copy_func,
                    dp->dnaname,
                    sprop->maxlength);
          }
          else {
            fprintf(f,
                    "    %s(data->%s, value, sizeof(data->%s));\n",
                    string_copy_func,
                    dp->dnaname,
                    dp->dnaname);
          }
        }
      }
      fprintf(f, "}\n\n");
      break;
    }
    case PROP_POINTER: {
      fprintf(f, "void %s(PointerRNA *ptr, PointerRNA value, struct ReportList *reports)\n", func);
      fprintf(f, "{\n");
      if (manualfunc) {
        fprintf(f, "    %s(ptr, value, reports);\n", manualfunc);
      }
      else {
        rna_print_data_get(f, dp);

        if (prop->flag & PROP_ID_SELF_CHECK) {
          rna_print_id_get(f, dp);
          fprintf(f, "    if (id == value.data) {\n");
          fprintf(f, "      return;\n");
          fprintf(f, "    }\n");
        }

        if (prop->flag & PROP_ID_REFCOUNT) {
          fprintf(f, "\n    if (data->%s) {\n", dp->dnaname);
          fprintf(f, "        id_us_min((ID *)data->%s);\n", dp->dnaname);
          fprintf(f, "    }\n");
          fprintf(f, "    if (value.data) {\n");
          fprintf(f, "        id_us_plus((ID *)value.data);\n");
          fprintf(f, "    }\n");
        }
        else {
          PointerPropertyRNA *pprop = (PointerPropertyRNA *)dp->prop;
          StructRNA *type = (pprop->type) ? rna_find_struct((const char *)pprop->type) : NULL;
          if (type && (type->flag & STRUCT_ID)) {
            fprintf(f, "    if (value.data) {\n");
            fprintf(f, "        id_lib_extern((ID *)value.data);\n");
            fprintf(f, "    }\n");
          }
        }

        fprintf(f, "    data->%s = value.data;\n", dp->dnaname);
      }
      fprintf(f, "}\n\n");
      break;
    }
    default:
      if (prop->arraydimension) {
        if (prop->flag & PROP_DYNAMIC) {
          fprintf(f, "void %s(PointerRNA *ptr, const %s values[])\n", func, rna_type_type(prop));
        }
        else {
          fprintf(f,
                  "void %s(PointerRNA *ptr, const %s values[%u])\n",
                  func,
                  rna_type_type(prop),
                  prop->totarraylength);
        }
        fprintf(f, "{\n");

        if (manualfunc) {
          fprintf(f, "    %s(ptr, values);\n", manualfunc);
        }
        else {
          rna_print_data_get(f, dp);

          if (prop->flag & PROP_DYNAMIC) {
            char *lenfunc = rna_alloc_function_name(
                srna->identifier, rna_safe_id(prop->identifier), "set_length");
            fprintf(f, "    unsigned int i, arraylen[RNA_MAX_ARRAY_DIMENSION];\n");
            fprintf(f, "    unsigned int len = %s(ptr, arraylen);\n\n", lenfunc);
            rna_clamp_value_range(f, prop);
            fprintf(f, "    for (i = 0; i < len; i++) {\n");
            MEM_freeN(lenfunc);
          }
          else {
            fprintf(f, "    unsigned int i;\n\n");
            rna_clamp_value_range(f, prop);
            fprintf(f, "    for (i = 0; i < %u; i++) {\n", prop->totarraylength);
          }

          if (dp->dnaarraylength == 1) {
            if (prop->type == PROP_BOOLEAN && dp->booleanbit) {
              fprintf(f,
                      "        if (%svalues[i]) { data->%s |= (",
                      (dp->booleannegative) ? "!" : "",
                      dp->dnaname);
              rna_int_print(f, dp->booleanbit);
              fprintf(f, " << i); }\n");
              fprintf(f, "        else { data->%s &= ~(", dp->dnaname);
              rna_int_print(f, dp->booleanbit);
              fprintf(f, " << i); }\n");
            }
            else {
              fprintf(
                  f, "        (&data->%s)[i] = %s", dp->dnaname, (dp->booleannegative) ? "!" : "");
              rna_clamp_value(f, prop, 1);
            }
          }
          else {
            if (prop->type == PROP_BOOLEAN && dp->booleanbit) {
              fprintf(f,
                      "        if (%svalues[i]) { data->%s[i] |= ",
                      (dp->booleannegative) ? "!" : "",
                      dp->dnaname);
              rna_int_print(f, dp->booleanbit);
              fprintf(f, "; }\n");
              fprintf(f, "        else { data->%s[i] &= ~", dp->dnaname);
              rna_int_print(f, dp->booleanbit);
              fprintf(f, "; }\n");
            }
            else if (rna_color_quantize(prop, dp)) {
              fprintf(
                  f, "        data->%s[i] = unit_float_to_uchar_clamp(values[i]);\n", dp->dnaname);
            }
            else {
              if (dp->dnatype) {
                fprintf(f,
                        "        ((%s *)data->%s)[i] = %s",
                        dp->dnatype,
                        dp->dnaname,
                        (dp->booleannegative) ? "!" : "");
              }
              else {
                fprintf(f,
                        "        (data->%s)[i] = %s",
                        dp->dnaname,
                        (dp->booleannegative) ? "!" : "");
              }
              rna_clamp_value(f, prop, 1);
            }
          }
          fprintf(f, "    }\n");
        }

#ifdef USE_RNA_RANGE_CHECK
        if (dp->dnaname && manualfunc == NULL) {
          if (dp->dnaarraylength == 1) {
            rna_clamp_value_range_check(f, prop, "data->", dp->dnaname);
          }
          else {
            rna_clamp_value_range_check(f, prop, "*data->", dp->dnaname);
          }
        }
#endif

        fprintf(f, "}\n\n");
      }
      else {
        fprintf(f, "void %s(PointerRNA *ptr, %s value)\n", func, rna_type_type(prop));
        fprintf(f, "{\n");

        if (manualfunc) {
          fprintf(f, "    %s(ptr, value);\n", manualfunc);
        }
        else {
          rna_print_data_get(f, dp);
          if (prop->type == PROP_BOOLEAN && dp->booleanbit) {
            fprintf(f,
                    "    if (%svalue) { data->%s |= ",
                    (dp->booleannegative) ? "!" : "",
                    dp->dnaname);
            rna_int_print(f, dp->booleanbit);
            fprintf(f, "; }\n");
            fprintf(f, "    else { data->%s &= ~", dp->dnaname);
            rna_int_print(f, dp->booleanbit);
            fprintf(f, "; }\n");
          }
          else if (prop->type == PROP_ENUM && dp->enumbitflags) {
            fprintf(f, "    data->%s &= ~", dp->dnaname);
            rna_int_print(f, rna_enum_bitmask(prop));
            fprintf(f, ";\n");
            fprintf(f, "    data->%s |= value;\n", dp->dnaname);
          }
          else {
            rna_clamp_value_range(f, prop);
            fprintf(f, "    data->%s = %s", dp->dnaname, (dp->booleannegative) ? "!" : "");
            rna_clamp_value(f, prop, 0);
          }
        }

#ifdef USE_RNA_RANGE_CHECK
        if (dp->dnaname && manualfunc == NULL) {
          rna_clamp_value_range_check(f, prop, "data->", dp->dnaname);
        }
#endif

        fprintf(f, "}\n\n");
      }
      break;
  }

  return func;
}

static char *rna_def_property_set_func(
    FILE *f, StructRNA *srna, PropertyRNA *prop, PropertyDefRNA *dp, const char *manualfunc)
{
  char *func;

  if (!(prop->flag & PROP_EDITABLE)) {
    return NULL;
  }
  if (prop->flag & PROP_IDPROPERTY && manualfunc == NULL) {
    return NULL;
  }

  if (!manualfunc) {
    if (!dp->dnastructname || !dp->dnaname) {
      if (prop->flag & PROP_EDITABLE) {
        CLOG_ERROR(&LOG, "%s.%s has no valid dna info.", srna->identifier, prop->identifier);
        DefRNA.error = true;
      }
      return NULL;
    }
  }

  func = rna_alloc_function_name(srna->identifier, rna_safe_id(prop->identifier), "set");

  switch (prop->type) {
    case PROP_STRING: {
      StringPropertyRNA *sprop = (StringPropertyRNA *)prop;
      fprintf(f, "void %s(PointerRNA *ptr, const char *value)\n", func);
      fprintf(f, "{\n");
      if (manualfunc) {
        fprintf(f, "    %s(ptr, value);\n", manualfunc);
      }
      else {
        const PropertySubType subtype = prop->subtype;
        const char *string_copy_func =
            ELEM(subtype, PROP_FILEPATH, PROP_DIRPATH, PROP_FILENAME, PROP_BYTESTRING) ?
                "BLI_strncpy" :
                "BLI_strncpy_utf8";

        rna_print_data_get(f, dp);

        if (dp->dnapointerlevel == 1) {
          /* Handle allocated char pointer properties. */
          fprintf(
              f, "    if (data->%s != NULL) { MEM_freeN(data->%s); }\n", dp->dnaname, dp->dnaname);
          fprintf(f, "    const int length = strlen(value);\n");
          fprintf(f, "    data->%s = MEM_mallocN(length + 1, __func__);\n", dp->dnaname);
          fprintf(f, "    %s(data->%s, value, length + 1);\n", string_copy_func, dp->dnaname);
        }
        else {
          /* Handle char array properties. */
          if (sprop->maxlength) {
            fprintf(f,
                    "    %s(data->%s, value, %d);\n",
                    string_copy_func,
                    dp->dnaname,
                    sprop->maxlength);
          }
          else {
            fprintf(f,
                    "    %s(data->%s, value, sizeof(data->%s));\n",
                    string_copy_func,
                    dp->dnaname,
                    dp->dnaname);
          }
        }
      }
      fprintf(f, "}\n\n");
      break;
    }
    case PROP_POINTER: {
      fprintf(f, "void %s(PointerRNA *ptr, PointerRNA value, struct ReportList *reports)\n", func);
      fprintf(f, "{\n");
      if (manualfunc) {
        fprintf(f, "    %s(ptr, value, reports);\n", manualfunc);
      }
      else {
        rna_print_data_get(f, dp);

        if (prop->flag & PROP_ID_SELF_CHECK) {
          rna_print_id_get(f, dp);
          fprintf(f, "    if (id == value.data) {\n");
          fprintf(f, "      return;\n");
          fprintf(f, "    }\n");
        }

        if (prop->flag & PROP_ID_REFCOUNT) {
          fprintf(f, "\n    if (data->%s) {\n", dp->dnaname);
          fprintf(f, "        id_us_min((ID *)data->%s);\n", dp->dnaname);
          fprintf(f, "    }\n");
          fprintf(f, "    if (value.data) {\n");
          fprintf(f, "        id_us_plus((ID *)value.data);\n");
          fprintf(f, "    }\n");
        }
        else {
          PointerPropertyRNA *pprop = (PointerPropertyRNA *)dp->prop;
          StructRNA *type = (pprop->type) ? rna_find_struct((const char *)pprop->type) : NULL;
          if (type && (type->flag & STRUCT_ID)) {
            fprintf(f, "    if (value.data) {\n");
            fprintf(f, "        id_lib_extern((ID *)value.data);\n");
            fprintf(f, "    }\n");
          }
        }

        fprintf(f, "    data->%s = value.data;\n", dp->dnaname);
      }
      fprintf(f, "}\n\n");
      break;
    }
    default:
      if (prop->arraydimension) {
        if (prop->flag & PROP_DYNAMIC) {
          fprintf(f, "void %s(PointerRNA *ptr, const %s values[])\n", func, rna_type_type(prop));
        }
        else {
          fprintf(f,
                  "void %s(PointerRNA *ptr, const %s values[%u])\n",
                  func,
                  rna_type_type(prop),
                  prop->totarraylength);
        }
        fprintf(f, "{\n");

        if (manualfunc) {
          fprintf(f, "    %s(ptr, values);\n", manualfunc);
        }
        else {
          rna_print_data_get(f, dp);

          if (prop->flag & PROP_DYNAMIC) {
            char *lenfunc = rna_alloc_function_name(
                srna->identifier, rna_safe_id(prop->identifier), "set_length");
            fprintf(f, "    unsigned int i, arraylen[RNA_MAX_ARRAY_DIMENSION];\n");
            fprintf(f, "    unsigned int len = %s(ptr, arraylen);\n\n", lenfunc);
            rna_clamp_value_range(f, prop);
            fprintf(f, "    for (i = 0; i < len; i++) {\n");
            MEM_freeN(lenfunc);
          }
          else {
            fprintf(f, "    unsigned int i;\n\n");
            rna_clamp_value_range(f, prop);
            fprintf(f, "    for (i = 0; i < %u; i++) {\n", prop->totarraylength);
          }

          if (dp->dnaarraylength == 1) {
            if (prop->type == PROP_BOOLEAN && dp->booleanbit) {
              fprintf(f,
                      "        if (%svalues[i]) { data->%s |= (",
                      (dp->booleannegative) ? "!" : "",
                      dp->dnaname);
              rna_int_print(f, dp->booleanbit);
              fprintf(f, " << i); }\n");
              fprintf(f, "        else { data->%s &= ~(", dp->dnaname);
              rna_int_print(f, dp->booleanbit);
              fprintf(f, " << i); }\n");
            }
            else {
              fprintf(
                  f, "        (&data->%s)[i] = %s", dp->dnaname, (dp->booleannegative) ? "!" : "");
              rna_clamp_value(f, prop, 1);
            }
          }
          else {
            if (prop->type == PROP_BOOLEAN && dp->booleanbit) {
              fprintf(f,
                      "        if (%svalues[i]) { data->%s[i] |= ",
                      (dp->booleannegative) ? "!" : "",
                      dp->dnaname);
              rna_int_print(f, dp->booleanbit);
              fprintf(f, "; }\n");
              fprintf(f, "        else { data->%s[i] &= ~", dp->dnaname);
              rna_int_print(f, dp->booleanbit);
              fprintf(f, "; }\n");
            }
            else if (rna_color_quantize(prop, dp)) {
              fprintf(
                  f, "        data->%s[i] = unit_float_to_uchar_clamp(values[i]);\n", dp->dnaname);
            }
            else {
              if (dp->dnatype) {
                fprintf(f,
                        "        ((%s *)data->%s)[i] = %s",
                        dp->dnatype,
                        dp->dnaname,
                        (dp->booleannegative) ? "!" : "");
              }
              else {
                fprintf(f,
                        "        (data->%s)[i] = %s",
                        dp->dnaname,
                        (dp->booleannegative) ? "!" : "");
              }
              rna_clamp_value(f, prop, 1);
            }
          }
          fprintf(f, "    }\n");
        }

#ifdef USE_RNA_RANGE_CHECK
        if (dp->dnaname && manualfunc == NULL) {
          if (dp->dnaarraylength == 1) {
            rna_clamp_value_range_check(f, prop, "data->", dp->dnaname);
          }
          else {
            rna_clamp_value_range_check(f, prop, "*data->", dp->dnaname);
          }
        }
#endif

        fprintf(f, "}\n\n");
      }
      else {
        fprintf(f, "void %s(PointerRNA *ptr, %s value)\n", func, rna_type_type(prop));
        fprintf(f, "{\n");

        if (manualfunc) {
          fprintf(f, "    %s(ptr, value);\n", manualfunc);
        }
        else {
          rna_print_data_get(f, dp);
          if (prop->type == PROP_BOOLEAN && dp->booleanbit) {
            fprintf(f,
                    "    if (%svalue) { data->%s |= ",
                    (dp->booleannegative) ? "!" : "",
                    dp->dnaname);
            rna_int_print(f, dp->booleanbit);
            fprintf(f, "; }\n");
            fprintf(f, "    else { data->%s &= ~", dp->dnaname);
            rna_int_print(f, dp->booleanbit);
            fprintf(f, "; }\n");
          }
          else if (prop->type == PROP_ENUM && dp->enumbitflags) {
            fprintf(f, "    data->%s &= ~", dp->dnaname);
            rna_int_print(f, rna_enum_bitmask(prop));
            fprintf(f, ";\n");
            fprintf(f, "    data->%s |= value;\n", dp->dnaname);
          }
          else {
            rna_clamp_value_range(f, prop);
            fprintf(f, "    data->%s = %s", dp->dnaname, (dp->booleannegative) ? "!" : "");
            rna_clamp_value(f, prop, 0);
          }
        }

#ifdef USE_RNA_RANGE_CHECK
        if (dp->dnaname && manualfunc == NULL) {
          rna_clamp_value_range_check(f, prop, "data->", dp->dnaname);
        }
#endif

        fprintf(f, "}\n\n");
      }
      break;
  }

  return func;
}

static char *rna_def_property_length_func(
    FILE *f, StructRNA *srna, PropertyRNA *prop, PropertyDefRNA *dp, const char *manualfunc)
{
  char *func = NULL;

  if (prop->flag & PROP_IDPROPERTY && manualfunc == NULL) {
    return NULL;
  }

  if (prop->type == PROP_STRING) {
    if (!manualfunc) {
      if (!dp->dnastructname || !dp->dnaname) {
        CLOG_ERROR(&LOG, "%s.%s has no valid dna info.", srna->identifier, prop->identifier);
        DefRNA.error = true;
        return NULL;
      }
    }

    func = rna_alloc_function_name(srna->identifier, rna_safe_id(prop->identifier), "length");

    fprintf(f, "int %s(PointerRNA *ptr)\n", func);
    fprintf(f, "{\n");
    if (manualfunc) {
      fprintf(f, "    return %s(ptr);\n", manualfunc);
    }
    else {
      rna_print_data_get(f, dp);
      if (dp->dnapointerlevel == 1) {
        /* Handle allocated char pointer properties. */
        fprintf(f,
                "    return (data->%s == NULL) ? 0 : strlen(data->%s);\n",
                dp->dnaname,
                dp->dnaname);
      }
      else {
        /* Handle char array properties. */
        fprintf(f, "    return strlen(data->%s);\n", dp->dnaname);
      }
    }
    fprintf(f, "}\n\n");
  }
  else if (prop->type == PROP_COLLECTION) {
    if (!manualfunc) {
      if (prop->type == PROP_COLLECTION &&
          (!(dp->dnalengthname || dp->dnalengthfixed) || !dp->dnaname)) {
        CLOG_ERROR(&LOG, "%s.%s has no valid dna info.", srna->identifier, prop->identifier);
        DefRNA.error = true;
        return NULL;
      }
    }

    func = rna_alloc_function_name(srna->identifier, rna_safe_id(prop->identifier), "length");

    fprintf(f, "int %s(PointerRNA *ptr)\n", func);
    fprintf(f, "{\n");
    if (manualfunc) {
      fprintf(f, "    return %s(ptr);\n", manualfunc);
    }
    else {
      if (dp->dnaarraylength <= 1 || dp->dnalengthname) {
        rna_print_data_get(f, dp);
      }

      if (dp->dnaarraylength > 1) {
        fprintf(f, "    return ");
      }
      else {
        fprintf(f, "    return (data->%s == NULL) ? 0 : ", dp->dnaname);
      }

      if (dp->dnalengthname) {
        fprintf(f, "data->%s;\n", dp->dnalengthname);
      }
      else {
        fprintf(f, "%d;\n", dp->dnalengthfixed);
      }
    }
    fprintf(f, "}\n\n");
  }

  return func;
}

static char *rna_def_property_begin_func(
    FILE *f, StructRNA *srna, PropertyRNA *prop, PropertyDefRNA *dp, const char *manualfunc)
{
  char *func, *getfunc;

  if (prop->flag & PROP_IDPROPERTY && manualfunc == NULL) {
    return NULL;
  }

  if (!manualfunc) {
    if (!dp->dnastructname || !dp->dnaname) {
      CLOG_ERROR(&LOG, "%s.%s has no valid dna info.", srna->identifier, prop->identifier);
      DefRNA.error = true;
      return NULL;
    }
  }

  func = rna_alloc_function_name(srna->identifier, rna_safe_id(prop->identifier), "begin");

  fprintf(f, "void %s(CollectionPropertyIterator *iter, PointerRNA *ptr)\n", func);
  fprintf(f, "{\n");

  if (!manualfunc) {
    rna_print_data_get(f, dp);
  }

  fprintf(f, "\n    memset(iter, 0, sizeof(*iter));\n");
  fprintf(f, "    iter->parent = *ptr;\n");
  fprintf(f, "    iter->prop = (PropertyRNA *)&rna_%s_%s;\n", srna->identifier, prop->identifier);

  if (dp->dnalengthname || dp->dnalengthfixed) {
    if (manualfunc) {
      fprintf(f, "\n    %s(iter, ptr);\n", manualfunc);
    }
    else {
      if (dp->dnalengthname) {
        fprintf(f,
                "\n    rna_iterator_array_begin(iter, data->%s, sizeof(data->%s[0]), data->%s, 0, "
                "NULL);\n",
                dp->dnaname,
                dp->dnaname,
                dp->dnalengthname);
      }
      else {
        fprintf(
            f,
            "\n    rna_iterator_array_begin(iter, data->%s, sizeof(data->%s[0]), %d, 0, NULL);\n",
            dp->dnaname,
            dp->dnaname,
            dp->dnalengthfixed);
      }
    }
  }
  else {
    if (manualfunc) {
      fprintf(f, "\n    %s(iter, ptr);\n", manualfunc);
    }
    else if (dp->dnapointerlevel == 0) {
      fprintf(f, "\n    rna_iterator_listbase_begin(iter, &data->%s, NULL);\n", dp->dnaname);
    }
    else {
      fprintf(f, "\n    rna_iterator_listbase_begin(iter, data->%s, NULL);\n", dp->dnaname);
    }
  }

  getfunc = rna_alloc_function_name(srna->identifier, rna_safe_id(prop->identifier), "get");

  fprintf(f, "\n    if (iter->valid) {\n");
  fprintf(f, "        iter->ptr = %s(iter);", getfunc);
  fprintf(f, "\n    }\n");

  fprintf(f, "}\n\n");

  return func;
}

static char *rna_def_property_lookup_int_func(FILE *f,
                                              StructRNA *srna,
                                              PropertyRNA *prop,
                                              PropertyDefRNA *dp,
                                              const char *manualfunc,
                                              const char *nextfunc)
{
  /* note on indices, this is for external functions and ignores skipped values.
   * so the index can only be checked against the length when there is no 'skip' function. */
  char *func;

  if (prop->flag & PROP_IDPROPERTY && manualfunc == NULL) {
    return NULL;
  }

  if (!manualfunc) {
    if (!dp->dnastructname || !dp->dnaname) {
      return NULL;
    }

    /* only supported in case of standard next functions */
    if (STREQ(nextfunc, "rna_iterator_array_next")) {
    }
    else if (STREQ(nextfunc, "rna_iterator_listbase_next")) {
    }
    else {
      return NULL;
    }
  }

  func = rna_alloc_function_name(srna->identifier, rna_safe_id(prop->identifier), "lookup_int");

  fprintf(f, "int %s(PointerRNA *ptr, int index, PointerRNA *r_ptr)\n", func);
  fprintf(f, "{\n");

  if (manualfunc) {
    fprintf(f, "\n    return %s(ptr, index, r_ptr);\n", manualfunc);
    fprintf(f, "}\n\n");
    return func;
  }

  fprintf(f, "    int found = 0;\n");
  fprintf(f, "    CollectionPropertyIterator iter;\n\n");

  fprintf(f, "    %s_%s_begin(&iter, ptr);\n\n", srna->identifier, rna_safe_id(prop->identifier));
  fprintf(f, "    if (iter.valid) {\n");

  if (STREQ(nextfunc, "rna_iterator_array_next")) {
    fprintf(f, "        ArrayIterator *internal = &iter.internal.array;\n");
    fprintf(f, "        if (index < 0 || index >= internal->length) {\n");
    fprintf(f, "#ifdef __GNUC__\n");
    fprintf(f,
            "            printf(\"Array iterator out of range: %%s (index %%d)\\n\", __func__, "
            "index);\n");
    fprintf(f, "#else\n");
    fprintf(f, "            printf(\"Array iterator out of range: (index %%d)\\n\", index);\n");
    fprintf(f, "#endif\n");
    fprintf(f, "        }\n");
    fprintf(f, "        else if (internal->skip) {\n");
    fprintf(f, "            while (index-- > 0 && iter.valid) {\n");
    fprintf(f, "                rna_iterator_array_next(&iter);\n");
    fprintf(f, "            }\n");
    fprintf(f, "            found = (index == -1 && iter.valid);\n");
    fprintf(f, "        }\n");
    fprintf(f, "        else {\n");
    fprintf(f, "            internal->ptr += internal->itemsize * index;\n");
    fprintf(f, "            found = 1;\n");
    fprintf(f, "        }\n");
  }
  else if (STREQ(nextfunc, "rna_iterator_listbase_next")) {
    fprintf(f, "        ListBaseIterator *internal = &iter.internal.listbase;\n");
    fprintf(f, "        if (internal->skip) {\n");
    fprintf(f, "            while (index-- > 0 && iter.valid) {\n");
    fprintf(f, "                rna_iterator_listbase_next(&iter);\n");
    fprintf(f, "            }\n");
    fprintf(f, "            found = (index == -1 && iter.valid);\n");
    fprintf(f, "        }\n");
    fprintf(f, "        else {\n");
    fprintf(f, "            while (index-- > 0 && internal->link) {\n");
    fprintf(f, "                internal->link = internal->link->next;\n");
    fprintf(f, "            }\n");
    fprintf(f, "            found = (index == -1 && internal->link);\n");
    fprintf(f, "        }\n");
  }

  fprintf(f,
          "        if (found) { *r_ptr = %s_%s_get(&iter); }\n",
          srna->identifier,
          rna_safe_id(prop->identifier));
  fprintf(f, "    }\n\n");
  fprintf(f, "    %s_%s_end(&iter);\n\n", srna->identifier, rna_safe_id(prop->identifier));

  fprintf(f, "    return found;\n");

#if 0
  rna_print_data_get(f, dp);
  item_type = (cprop->item_type) ? (const char *)cprop->item_type : "UnknownType";

  if (dp->dnalengthname || dp->dnalengthfixed) {
    if (dp->dnalengthname) {
      fprintf(f,
              "\n    rna_array_lookup_int(ptr, &RNA_%s, data->%s, sizeof(data->%s[0]), data->%s, "
              "index);\n",
              item_type,
              dp->dnaname,
              dp->dnaname,
              dp->dnalengthname);
    }
    else {
      fprintf(
          f,
          "\n    rna_array_lookup_int(ptr, &RNA_%s, data->%s, sizeof(data->%s[0]), %d, index);\n",
          item_type,
          dp->dnaname,
          dp->dnaname,
          dp->dnalengthfixed);
    }
  }
  else {
    if (dp->dnapointerlevel == 0) {
      fprintf(f,
              "\n    return rna_listbase_lookup_int(ptr, &RNA_%s, &data->%s, index);\n",
              item_type,
              dp->dnaname);
    }
    else {
      fprintf(f,
              "\n    return rna_listbase_lookup_int(ptr, &RNA_%s, data->%s, index);\n",
              item_type,
              dp->dnaname);
    }
  }
#endif

  fprintf(f, "}\n\n");

  return func;
}

static char *rna_def_property_lookup_string_func(FILE *f,
                                                 StructRNA *srna,
                                                 PropertyRNA *prop,
                                                 PropertyDefRNA *dp,
                                                 const char *manualfunc,
                                                 const char *item_type)
{
  char *func;
  StructRNA *item_srna, *item_name_base;
  PropertyRNA *item_name_prop;
  const int namebuflen = 1024;

  if (prop->flag & PROP_IDPROPERTY && manualfunc == NULL) {
    return NULL;
  }

  if (!manualfunc) {
    if (!dp->dnastructname || !dp->dnaname) {
      return NULL;
    }

    /* only supported for collection items with name properties */
    item_srna = rna_find_struct(item_type);
    if (item_srna && item_srna->nameproperty) {
      item_name_prop = item_srna->nameproperty;
      item_name_base = item_srna;
      while (item_name_base->base && item_name_base->base->nameproperty == item_name_prop) {
        item_name_base = item_name_base->base;
      }
    }
    else {
      return NULL;
    }
  }

  func = rna_alloc_function_name(srna->identifier, rna_safe_id(prop->identifier), "lookup_string");

  fprintf(f, "int %s(PointerRNA *ptr, const char *key, PointerRNA *r_ptr)\n", func);
  fprintf(f, "{\n");

  if (manualfunc) {
    fprintf(f, "    return %s(ptr, key, r_ptr);\n", manualfunc);
    fprintf(f, "}\n\n");
    return func;
  }

  /* XXX extern declaration could be avoid by including RNA_blender.h, but this has lots of unknown
   * DNA types in functions, leading to conflicting function signatures.
   */
  fprintf(f,
          "    extern int %s_%s_length(PointerRNA *);\n",
          item_name_base->identifier,
          rna_safe_id(item_name_prop->identifier));
  fprintf(f,
          "    extern void %s_%s_get(PointerRNA *, char *);\n\n",
          item_name_base->identifier,
          rna_safe_id(item_name_prop->identifier));

  fprintf(f, "    bool found = false;\n");
  fprintf(f, "    CollectionPropertyIterator iter;\n");
  fprintf(f, "    char namebuf[%d];\n", namebuflen);
  fprintf(f, "    char *name;\n\n");

  fprintf(f, "    %s_%s_begin(&iter, ptr);\n\n", srna->identifier, rna_safe_id(prop->identifier));

  fprintf(f, "    while (iter.valid) {\n");
  fprintf(f, "        if (iter.ptr.data) {\n");
  fprintf(f,
          "            int namelen = %s_%s_length(&iter.ptr);\n",
          item_name_base->identifier,
          rna_safe_id(item_name_prop->identifier));
  fprintf(f, "            if (namelen < %d) {\n", namebuflen);
  fprintf(f,
          "                %s_%s_get(&iter.ptr, namebuf);\n",
          item_name_base->identifier,
          rna_safe_id(item_name_prop->identifier));
  fprintf(f, "                if (strcmp(namebuf, key) == 0) {\n");
  fprintf(f, "                    found = true;\n");
  fprintf(f, "                    *r_ptr = iter.ptr;\n");
  fprintf(f, "                    break;\n");
  fprintf(f, "                }\n");
  fprintf(f, "            }\n");
  fprintf(f, "            else {\n");
  fprintf(f, "                name = MEM_mallocN(namelen+1, \"name string\");\n");
  fprintf(f,
          "                %s_%s_get(&iter.ptr, name);\n",
          item_name_base->identifier,
          rna_safe_id(item_name_prop->identifier));
  fprintf(f, "                if (strcmp(name, key) == 0) {\n");
  fprintf(f, "                    MEM_freeN(name);\n\n");
  fprintf(f, "                    found = true;\n");
  fprintf(f, "                    *r_ptr = iter.ptr;\n");
  fprintf(f, "                    break;\n");
  fprintf(f, "                }\n");
  fprintf(f, "                else {\n");
  fprintf(f, "                    MEM_freeN(name);\n");
  fprintf(f, "                }\n");
  fprintf(f, "            }\n");
  fprintf(f, "        }\n");
  fprintf(f, "        %s_%s_next(&iter);\n", srna->identifier, rna_safe_id(prop->identifier));
  fprintf(f, "    }\n");
  fprintf(f, "    %s_%s_end(&iter);\n\n", srna->identifier, rna_safe_id(prop->identifier));

  fprintf(f, "    return found;\n");
  fprintf(f, "}\n\n");

  return func;
}

static char *rna_def_property_next_func(FILE *f,
                                        StructRNA *srna,
                                        PropertyRNA *prop,
                                        PropertyDefRNA *UNUSED(dp),
                                        const char *manualfunc)
{
  char *func, *getfunc;

  if (prop->flag & PROP_IDPROPERTY && manualfunc == NULL) {
    return NULL;
  }

  if (!manualfunc) {
    return NULL;
  }

  func = rna_alloc_function_name(srna->identifier, rna_safe_id(prop->identifier), "next");

  fprintf(f, "void %s(CollectionPropertyIterator *iter)\n", func);
  fprintf(f, "{\n");
  fprintf(f, "    %s(iter);\n", manualfunc);

  getfunc = rna_alloc_function_name(srna->identifier, rna_safe_id(prop->identifier), "get");

  fprintf(f, "\n    if (iter->valid) {\n");
  fprintf(f, "        iter->ptr = %s(iter);", getfunc);
  fprintf(f, "\n    }\n");

  fprintf(f, "}\n\n");

  return func;
}

static char *rna_def_property_end_func(FILE *f,
                                       StructRNA *srna,
                                       PropertyRNA *prop,
                                       PropertyDefRNA *UNUSED(dp),
                                       const char *manualfunc)
{
  char *func;

  if (prop->flag & PROP_IDPROPERTY && manualfunc == NULL) {
    return NULL;
  }

  func = rna_alloc_function_name(srna->identifier, rna_safe_id(prop->identifier), "end");

  fprintf(f, "void %s(CollectionPropertyIterator *iter)\n", func);
  fprintf(f, "{\n");
  if (manualfunc) {
    fprintf(f, "    %s(iter);\n", manualfunc);
  }
  fprintf(f, "}\n\n");

  return func;
}

static void rna_set_raw_property(PropertyDefRNA *dp, PropertyRNA *prop)
{
  if (dp->dnapointerlevel != 0) {
    return;
  }
  if (!dp->dnatype || !dp->dnaname || !dp->dnastructname) {
    return;
  }

  if (STREQ(dp->dnatype, "char")) {
    prop->rawtype = PROP_RAW_CHAR;
    prop->flag_internal |= PROP_INTERN_RAW_ACCESS;
  }
  else if (STREQ(dp->dnatype, "short")) {
    prop->rawtype = PROP_RAW_SHORT;
    prop->flag_internal |= PROP_INTERN_RAW_ACCESS;
  }
  else if (STREQ(dp->dnatype, "int")) {
    prop->rawtype = PROP_RAW_INT;
    prop->flag_internal |= PROP_INTERN_RAW_ACCESS;
  }
  else if (STREQ(dp->dnatype, "float")) {
    prop->rawtype = PROP_RAW_FLOAT;
    prop->flag_internal |= PROP_INTERN_RAW_ACCESS;
  }
  else if (STREQ(dp->dnatype, "double")) {
    prop->rawtype = PROP_RAW_DOUBLE;
    prop->flag_internal |= PROP_INTERN_RAW_ACCESS;
  }
}

static void rna_set_raw_offset(FILE *f, StructRNA *srna, PropertyRNA *prop)
{
  PropertyDefRNA *dp = rna_find_struct_property_def(srna, prop);

  fprintf(f, "\toffsetof(%s, %s), %d", dp->dnastructname, dp->dnaname, prop->rawtype);
}

static void rna_def_property_funcs(FILE *f, StructRNA *srna, PropertyDefRNA *dp)
{
  PropertyRNA *prop;

  prop = dp->prop;

  switch (prop->type) {
    case PROP_BOOLEAN: {
      BoolPropertyRNA *bprop = (BoolPropertyRNA *)prop;

      if (!prop->arraydimension) {
        if (!bprop->get && !bprop->set && !dp->booleanbit) {
          rna_set_raw_property(dp, prop);
        }

        bprop->get = (void *)rna_def_property_get_func(
            f, srna, prop, dp, (const char *)bprop->get);
        bprop->set = (void *)rna_def_property_set_func(
            f, srna, prop, dp, (const char *)bprop->set);
      }
      else {
        bprop->getarray = (void *)rna_def_property_get_func(
            f, srna, prop, dp, (const char *)bprop->getarray);
        bprop->setarray = (void *)rna_def_property_set_func(
            f, srna, prop, dp, (const char *)bprop->setarray);
      }
      break;
    }
    case PROP_INT: {
      IntPropertyRNA *iprop = (IntPropertyRNA *)prop;

      if (!prop->arraydimension) {
        if (!iprop->get && !iprop->set) {
          rna_set_raw_property(dp, prop);
        }

        iprop->get = (void *)rna_def_property_get_func(
            f, srna, prop, dp, (const char *)iprop->get);
        iprop->set = (void *)rna_def_property_set_func(
            f, srna, prop, dp, (const char *)iprop->set);
      }
      else {
        if (!iprop->getarray && !iprop->setarray) {
          rna_set_raw_property(dp, prop);
        }

        iprop->getarray = (void *)rna_def_property_get_func(
            f, srna, prop, dp, (const char *)iprop->getarray);
        iprop->setarray = (void *)rna_def_property_set_func(
            f, srna, prop, dp, (const char *)iprop->setarray);
      }
      break;
    }
    case PROP_FLOAT: {
      FloatPropertyRNA *fprop = (FloatPropertyRNA *)prop;

      if (!prop->arraydimension) {
        if (!fprop->get && !fprop->set) {
          rna_set_raw_property(dp, prop);
        }

        fprop->get = (void *)rna_def_property_get_func(
            f, srna, prop, dp, (const char *)fprop->get);
        fprop->set = (void *)rna_def_property_set_func(
            f, srna, prop, dp, (const char *)fprop->set);
      }
      else {
        if (!fprop->getarray && !fprop->setarray) {
          rna_set_raw_property(dp, prop);
        }

        fprop->getarray = (void *)rna_def_property_get_func(
            f, srna, prop, dp, (const char *)fprop->getarray);
        fprop->setarray = (void *)rna_def_property_set_func(
            f, srna, prop, dp, (const char *)fprop->setarray);
      }
      break;
    }
    case PROP_ENUM: {
      EnumPropertyRNA *eprop = (EnumPropertyRNA *)prop;

      if (!eprop->get && !eprop->set) {
        rna_set_raw_property(dp, prop);
      }

      eprop->get = (void *)rna_def_property_get_func(f, srna, prop, dp, (const char *)eprop->get);
      eprop->set = (void *)rna_def_property_set_func(f, srna, prop, dp, (const char *)eprop->set);
      break;
    }
    case PROP_STRING: {
      StringPropertyRNA *sprop = (StringPropertyRNA *)prop;

      sprop->get = (void *)rna_def_property_get_func(f, srna, prop, dp, (const char *)sprop->get);
      sprop->length = (void *)rna_def_property_length_func(
          f, srna, prop, dp, (const char *)sprop->length);
      sprop->set = (void *)rna_def_property_set_func(f, srna, prop, dp, (const char *)sprop->set);
      break;
    }
    case PROP_POINTER: {
      PointerPropertyRNA *pprop = (PointerPropertyRNA *)prop;

      pprop->get = (void *)rna_def_property_get_func(f, srna, prop, dp, (const char *)pprop->get);
      pprop->set = (void *)rna_def_property_set_func(f, srna, prop, dp, (const char *)pprop->set);
      if (!pprop->type) {
        CLOG_ERROR(
            &LOG, "%s.%s, pointer must have a struct type.", srna->identifier, prop->identifier);
        DefRNA.error = true;
      }
      break;
    }
    case PROP_COLLECTION: {
      CollectionPropertyRNA *cprop = (CollectionPropertyRNA *)prop;
      const char *nextfunc = (const char *)cprop->next;
      const char *item_type = (const char *)cprop->item_type;

      if (cprop->length) {
        /* always generate if we have a manual implementation */
        cprop->length = (void *)rna_def_property_length_func(
            f, srna, prop, dp, (const char *)cprop->length);
      }
      else if (dp->dnatype && STREQ(dp->dnatype, "ListBase")) {
        /* pass */
      }
      else if (dp->dnalengthname || dp->dnalengthfixed) {
        cprop->length = (void *)rna_def_property_length_func(
            f, srna, prop, dp, (const char *)cprop->length);
      }

      /* test if we can allow raw array access, if it is using our standard
       * array get/next function, we can be sure it is an actual array */
      if (cprop->next && cprop->get) {
        if (STREQ((const char *)cprop->next, "rna_iterator_array_next") &&
            STREQ((const char *)cprop->get, "rna_iterator_array_get")) {
          prop->flag_internal |= PROP_INTERN_RAW_ARRAY;
        }
      }

      cprop->get = (void *)rna_def_property_get_func(f, srna, prop, dp, (const char *)cprop->get);
      cprop->begin = (void *)rna_def_property_begin_func(
          f, srna, prop, dp, (const char *)cprop->begin);
      cprop->next = (void *)rna_def_property_next_func(
          f, srna, prop, dp, (const char *)cprop->next);
      cprop->end = (void *)rna_def_property_end_func(f, srna, prop, dp, (const char *)cprop->end);
      cprop->lookupint = (void *)rna_def_property_lookup_int_func(
          f, srna, prop, dp, (const char *)cprop->lookupint, nextfunc);
      cprop->lookupstring = (void *)rna_def_property_lookup_string_func(
          f, srna, prop, dp, (const char *)cprop->lookupstring, item_type);

      if (!(prop->flag & PROP_IDPROPERTY)) {
        if (!cprop->begin) {
          CLOG_ERROR(&LOG,
                     "%s.%s, collection must have a begin function.",
                     srna->identifier,
                     prop->identifier);
          DefRNA.error = true;
        }
        if (!cprop->next) {
          CLOG_ERROR(&LOG,
                     "%s.%s, collection must have a next function.",
                     srna->identifier,
                     prop->identifier);
          DefRNA.error = true;
        }
        if (!cprop->get) {
          CLOG_ERROR(&LOG,
                     "%s.%s, collection must have a get function.",
                     srna->identifier,
                     prop->identifier);
          DefRNA.error = true;
        }
      }
      if (!cprop->item_type) {
        CLOG_ERROR(&LOG,
                   "%s.%s, collection must have a struct type.",
                   srna->identifier,
                   prop->identifier);
        DefRNA.error = true;
      }
      break;
    }
  }
}

static void rna_def_property_funcs_header(FILE *f, StructRNA *srna, PropertyDefRNA *dp)
{
  PropertyRNA *prop;
  const char *func;

  prop = dp->prop;

  if (prop->flag & PROP_IDPROPERTY || prop->flag_internal & PROP_INTERN_BUILTIN) {
    return;
  }

  func = rna_alloc_function_name(srna->identifier, rna_safe_id(prop->identifier), "");

  switch (prop->type) {
    case PROP_BOOLEAN: {
      if (!prop->arraydimension) {
        fprintf(f, "bool %sget(PointerRNA *ptr);\n", func);
        fprintf(f, "void %sset(PointerRNA *ptr, bool value);\n", func);
      }
      else if (prop->arraydimension && prop->totarraylength) {
        fprintf(f, "void %sget(PointerRNA *ptr, bool values[%u]);\n", func, prop->totarraylength);
        fprintf(f,
                "void %sset(PointerRNA *ptr, const bool values[%u]);\n",
                func,
                prop->totarraylength);
      }
      else {
        fprintf(f, "void %sget(PointerRNA *ptr, bool values[]);\n", func);
        fprintf(f, "void %sset(PointerRNA *ptr, const bool values[]);\n", func);
      }
      break;
    }
    case PROP_INT: {
      if (!prop->arraydimension) {
        fprintf(f, "int %sget(PointerRNA *ptr);\n", func);
        fprintf(f, "void %sset(PointerRNA *ptr, int value);\n", func);
      }
      else if (prop->arraydimension && prop->totarraylength) {
        fprintf(f, "void %sget(PointerRNA *ptr, int values[%u]);\n", func, prop->totarraylength);
        fprintf(
            f, "void %sset(PointerRNA *ptr, const int values[%u]);\n", func, prop->totarraylength);
      }
      else {
        fprintf(f, "void %sget(PointerRNA *ptr, int values[]);\n", func);
        fprintf(f, "void %sset(PointerRNA *ptr, const int values[]);\n", func);
      }
      break;
    }
    case PROP_FLOAT: {
      if (!prop->arraydimension) {
        fprintf(f, "float %sget(PointerRNA *ptr);\n", func);
        fprintf(f, "void %sset(PointerRNA *ptr, float value);\n", func);
      }
      else if (prop->arraydimension && prop->totarraylength) {
        fprintf(f, "void %sget(PointerRNA *ptr, float values[%u]);\n", func, prop->totarraylength);
        fprintf(f,
                "void %sset(PointerRNA *ptr, const float values[%u]);\n",
                func,
                prop->totarraylength);
      }
      else {
        fprintf(f, "void %sget(PointerRNA *ptr, float values[]);\n", func);
        fprintf(f, "void %sset(PointerRNA *ptr, const float values[]);", func);
      }
      break;
    }
    case PROP_ENUM: {
      EnumPropertyRNA *eprop = (EnumPropertyRNA *)prop;
      int i;

      if (eprop->item && eprop->totitem) {
        fprintf(f, "enum {\n");

        for (i = 0; i < eprop->totitem; i++) {
          if (eprop->item[i].identifier[0]) {
            fprintf(f,
                    "\t%s_%s_%s = %d,\n",
                    srna->identifier,
                    prop->identifier,
                    eprop->item[i].identifier,
                    eprop->item[i].value);
          }
        }

        fprintf(f, "};\n\n");
      }

      fprintf(f, "int %sget(PointerRNA *ptr);\n", func);
      fprintf(f, "void %sset(PointerRNA *ptr, int value);\n", func);

      break;
    }
    case PROP_STRING: {
      StringPropertyRNA *sprop = (StringPropertyRNA *)prop;

      if (sprop->maxlength) {
        fprintf(
            f, "#define %s_%s_MAX %d\n\n", srna->identifier, prop->identifier, sprop->maxlength);
      }

      fprintf(f, "void %sget(PointerRNA *ptr, char *value);\n", func);
      fprintf(f, "int %slength(PointerRNA *ptr);\n", func);
      fprintf(f, "void %sset(PointerRNA *ptr, const char *value);\n", func);

      break;
    }
    case PROP_POINTER: {
      fprintf(f, "PointerRNA %sget(PointerRNA *ptr);\n", func);
      /*fprintf(f, "void %sset(PointerRNA *ptr, PointerRNA value);\n", func); */
      break;
    }
    case PROP_COLLECTION: {
      CollectionPropertyRNA *cprop = (CollectionPropertyRNA *)prop;
      fprintf(f, "void %sbegin(CollectionPropertyIterator *iter, PointerRNA *ptr);\n", func);
      fprintf(f, "void %snext(CollectionPropertyIterator *iter);\n", func);
      fprintf(f, "void %send(CollectionPropertyIterator *iter);\n", func);
      if (cprop->length) {
        fprintf(f, "int %slength(PointerRNA *ptr);\n", func);
      }
      if (cprop->lookupint) {
        fprintf(f, "int %slookup_int(PointerRNA *ptr, int key, PointerRNA *r_ptr);\n", func);
      }
      if (cprop->lookupstring) {
        fprintf(f,
                "int %slookup_string(PointerRNA *ptr, const char *key, PointerRNA *r_ptr);\n",
                func);
      }
      break;
    }
  }

  if (prop->getlength) {
    char funcname[2048];
    rna_construct_wrapper_function_name(
        funcname, sizeof(funcname), srna->identifier, prop->identifier, "get_length");
    fprintf(f, "int %s(PointerRNA *ptr, int *arraylen);\n", funcname);
  }

  fprintf(f, "\n");
}

static void rna_def_function_funcs_header(FILE *f, StructRNA *srna, FunctionDefRNA *dfunc)
{
  FunctionRNA *func = dfunc->func;
  char funcname[2048];

  rna_construct_wrapper_function_name(
      funcname, sizeof(funcname), srna->identifier, func->identifier, "func");
  rna_generate_static_parameter_prototypes(f, srna, dfunc, funcname, 1);
}

static void rna_def_property_funcs_header_cpp(FILE *f, StructRNA *srna, PropertyDefRNA *dp)
{
  PropertyRNA *prop;

  prop = dp->prop;

  if (prop->flag & PROP_IDPROPERTY || prop->flag_internal & PROP_INTERN_BUILTIN) {
    return;
  }

  /* Disabled for now to avoid MSVC compiler error due to large file size. */
#if 0
  if (prop->name && prop->description && prop->description[0] != '\0') {
    fprintf(f, "\t/* %s: %s */\n", prop->name, prop->description);
  }
  else if (prop->name) {
    fprintf(f, "\t/* %s */\n", prop->name);
  }
  else {
    fprintf(f, "\t/* */\n");
  }
#endif

  switch (prop->type) {
    case PROP_BOOLEAN: {
      if (!prop->arraydimension) {
        fprintf(f, "\tinline bool %s(void);\n", rna_safe_id(prop->identifier));
        fprintf(f, "\tinline void %s(bool value);", rna_safe_id(prop->identifier));
      }
      else if (prop->totarraylength) {
        fprintf(f,
                "\tinline Array<bool, %u> %s(void);\n",
                prop->totarraylength,
                rna_safe_id(prop->identifier));
        fprintf(f,
                "\tinline void %s(bool values[%u]);",
                rna_safe_id(prop->identifier),
                prop->totarraylength);
      }
      else if (prop->getlength) {
        fprintf(f, "\tinline DynamicArray<bool> %s(void);\n", rna_safe_id(prop->identifier));
        fprintf(f, "\tinline void %s(bool values[]);", rna_safe_id(prop->identifier));
      }
      break;
    }
    case PROP_INT: {
      if (!prop->arraydimension) {
        fprintf(f, "\tinline int %s(void);\n", rna_safe_id(prop->identifier));
        fprintf(f, "\tinline void %s(int value);", rna_safe_id(prop->identifier));
      }
      else if (prop->totarraylength) {
        fprintf(f,
                "\tinline Array<int, %u> %s(void);\n",
                prop->totarraylength,
                rna_safe_id(prop->identifier));
        fprintf(f,
                "\tinline void %s(int values[%u]);",
                rna_safe_id(prop->identifier),
                prop->totarraylength);
      }
      else if (prop->getlength) {
        fprintf(f, "\tinline DynamicArray<int> %s(void);\n", rna_safe_id(prop->identifier));
        fprintf(f, "\tinline void %s(int values[]);", rna_safe_id(prop->identifier));
      }
      break;
    }
    case PROP_FLOAT: {
      if (!prop->arraydimension) {
        fprintf(f, "\tinline float %s(void);\n", rna_safe_id(prop->identifier));
        fprintf(f, "\tinline void %s(float value);", rna_safe_id(prop->identifier));
      }
      else if (prop->totarraylength) {
        fprintf(f,
                "\tinline Array<float, %u> %s(void);\n",
                prop->totarraylength,
                rna_safe_id(prop->identifier));
        fprintf(f,
                "\tinline void %s(float values[%u]);",
                rna_safe_id(prop->identifier),
                prop->totarraylength);
      }
      else if (prop->getlength) {
        fprintf(f, "\tinline DynamicArray<float> %s(void);\n", rna_safe_id(prop->identifier));
        fprintf(f, "\tinline void %s(float values[]);", rna_safe_id(prop->identifier));
      }
      break;
    }
    case PROP_ENUM: {
      EnumPropertyRNA *eprop = (EnumPropertyRNA *)prop;
      int i;

      if (eprop->item) {
        fprintf(f, "\tenum %s_enum {\n", rna_safe_id(prop->identifier));

        for (i = 0; i < eprop->totitem; i++) {
          if (eprop->item[i].identifier[0]) {
            fprintf(f,
                    "\t\t%s_%s = %d,\n",
                    rna_safe_id(prop->identifier),
                    eprop->item[i].identifier,
                    eprop->item[i].value);
          }
        }

        fprintf(f, "\t};\n");
      }

      fprintf(f,
              "\tinline %s_enum %s(void);\n",
              rna_safe_id(prop->identifier),
              rna_safe_id(prop->identifier));
      fprintf(f,
              "\tinline void %s(%s_enum value);",
              rna_safe_id(prop->identifier),
              rna_safe_id(prop->identifier));
      break;
    }
    case PROP_STRING: {
      fprintf(f, "\tinline std::string %s(void);\n", rna_safe_id(prop->identifier));
      fprintf(f, "\tinline void %s(const std::string& value);", rna_safe_id(prop->identifier));
      break;
    }
    case PROP_POINTER: {
      PointerPropertyRNA *pprop = (PointerPropertyRNA *)dp->prop;

      if (pprop->type) {
        fprintf(
            f, "\tinline %s %s(void);", (const char *)pprop->type, rna_safe_id(prop->identifier));
      }
      else {
        fprintf(f, "\tinline %s %s(void);", "UnknownType", rna_safe_id(prop->identifier));
      }
      break;
    }
    case PROP_COLLECTION: {
      CollectionPropertyRNA *cprop = (CollectionPropertyRNA *)dp->prop;
      const char *collection_funcs = "DefaultCollectionFunctions";

      if (!(dp->prop->flag & PROP_IDPROPERTY || dp->prop->flag_internal & PROP_INTERN_BUILTIN) &&
          cprop->property.srna) {
        collection_funcs = (char *)cprop->property.srna;
      }

      if (cprop->item_type) {
        fprintf(f,
                "\tCOLLECTION_PROPERTY(%s, %s, %s, %s, %s, %s, %s)",
                collection_funcs,
                (const char *)cprop->item_type,
                srna->identifier,
                rna_safe_id(prop->identifier),
                (cprop->length ? "true" : "false"),
                (cprop->lookupint ? "true" : "false"),
                (cprop->lookupstring ? "true" : "false"));
      }
      else {
        fprintf(f,
                "\tCOLLECTION_PROPERTY(%s, %s, %s, %s, %s, %s, %s)",
                collection_funcs,
                "UnknownType",
                srna->identifier,
                rna_safe_id(prop->identifier),
                (cprop->length ? "true" : "false"),
                (cprop->lookupint ? "true" : "false"),
                (cprop->lookupstring ? "true" : "false"));
      }
      break;
    }
  }

  fprintf(f, "\n");
}
