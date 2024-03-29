/*
 * The code generator module for SIP.
 *
 * Copyright (c) 2023 Riverbank Computing Limited <info@riverbankcomputing.com>
 *
 * This file is part of SIP.
 *
 * This copy of SIP is licensed for use under the terms of the SIP License
 * Agreement.  See the file LICENSE for more details.
 *
 * This copy of SIP may also used under the terms of the GNU General Public
 * License v2 or v3 as published by the Free Software Foundation which can be
 * found in the files LICENSE-GPL2 and LICENSE-GPL3 included in this package.
 *
 * SIP is supplied WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */


#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#include "sip.h"


/* Return the base (ie. C/C++) name of a super-type or meta-type. */
#define smtypeName(sm)          (strrchr((sm)->name->text, '.') + 1)

/* Return TRUE if a wrapped variable can be set. */
#define canSetVariable(vd)      (!noSetter(vd) && ((vd)->type.nrderefs != 0 || !isConstArg(&(vd)->type)))

/* Return TRUE if a module implements Qt support. */
#define moduleSupportsQt(pt, mod)   ((pt)->qobject_cd != NULL && (pt)->qobject_cd->iff->module == (mod))


/* Control what generateCalledArgs() actually generates. */
typedef enum {
    Declaration,
    Definition
} funcArgType;


typedef enum {
    StripNone,
    StripGlobal,
    StripScope
} StripAction;


/* An entry in the sorted array of methods. */
typedef struct {
    memberDef *md;                      /* The method. */
} sortedMethTab;


static int currentLineNr;               /* Current output line number. */
static const char *currentFileName;     /* Current output file name. */
static int previousLineNr;              /* Previous output line number. */
static const char *previousFileName;    /* Previous output file name. */
static int exceptions;                  /* Set if exceptions are enabled. */
static int tracing;                     /* Set if tracing is enabled. */
static int generating_c;                /* Set if generating C. */
static int release_gil;                 /* Set if always releasing the GIL. */
static int prcode_xml = FALSE;          /* Set if prcode is XML aware. */
static int docstrings;                  /* Set if generating docstrings. */


static const char *generateInternalAPIHeader(sipSpec *pt, moduleDef *mod,
        const char *codeDir, stringList *needed_qualifiers, stringList *xsl,
        int py_debug);
static const char *generateCpp(sipSpec *pt, moduleDef *mod,
        const char *codeDir, stringList **generated, const char *srcSuffix,
        int parts, stringList *needed_qualifiers, stringList *xsl,
        int py_debug);
static int generateCompositeCpp(sipSpec *pt, const char *codeDir,
        stringList **generated, int py_debug);
static void generateSipAPI(moduleDef *mod, FILE *fp);
static void generateSipImportVariables(FILE *fp);
static void generateModInitStart(moduleDef *mod, int gen_c, FILE *fp);
static void generateModDefinition(moduleDef *mod, const char *methods,
        FILE *fp);
static void generateModDocstring(moduleDef *mod, FILE *fp);
static int generateIfaceCpp(sipSpec *, stringList **generated, int,
        ifaceFileDef *, int, const char *, const char *, FILE *);
static int generateMappedTypeCpp(mappedTypeDef *mtd, sipSpec *pt, FILE *fp);
static void generateImportedMappedTypeAPI(mappedTypeDef *mtd, moduleDef *mod,
        FILE *fp);
static void generateMappedTypeAPI(sipSpec *pt, mappedTypeDef *mtd, FILE *fp);
static int generateClassCpp(classDef *cd, sipSpec *pt, int py_debug, FILE *fp);
static void generateImportedClassAPI(classDef *cd, moduleDef *mod, FILE *fp);
static void generateClassAPI(classDef *cd, sipSpec *pt, FILE *fp);
static int generateClassFunctions(sipSpec *pt, moduleDef *mod, classDef *cd,
        int py_debug, FILE *fp);
static int generateShadowCode(sipSpec *pt, moduleDef *mod, classDef *cd,
        FILE *fp);
static int generateFunction(sipSpec *, memberDef *, overDef *, classDef *,
        classDef *, moduleDef *, FILE *);
static int generateFunctionBody(overDef *, classDef *, mappedTypeDef *,
        classDef *, int deref, moduleDef *, FILE *);
static void generatePyObjects(sipSpec *pt, moduleDef *mod, FILE *fp);
static int generateTypeDefinition(sipSpec *pt, classDef *cd, int py_debug,
        FILE *fp);
static int generateTypeInit(classDef *, moduleDef *, FILE *);
static void generateCppCodeBlock(codeBlockList *cbl, FILE *fp);
static void generateUsedIncludes(ifaceFileList *iffl, FILE *fp);
static void generateModuleAPI(sipSpec *pt, moduleDef *mod, FILE *fp);
static void generateImportedModuleAPI(sipSpec *pt, moduleDef *mod,
        moduleDef *immod, FILE *fp);
static void generateShadowClassDeclaration(sipSpec *, classDef *, FILE *);
static codeBlockList *convertToCode(argDef *ad);
static void deleteOuts(moduleDef *mod, signatureDef *sd, FILE *fp);
static void deleteTemps(moduleDef *mod, signatureDef *sd, FILE *fp);
static void gc_ellipsis(signatureDef *sd, FILE *fp);
static void generateCallArgs(moduleDef *, signatureDef *, signatureDef *,
        FILE *);
static void generateCalledArgs(moduleDef *, ifaceFileDef *, signatureDef *,
        funcArgType, FILE *);
static void generateVariable(moduleDef *, ifaceFileDef *, argDef *, int,
        FILE *);
static void generateNamedValueType(ifaceFileDef *, argDef *, char *, FILE *);
static void generateOverloadDecl(FILE *fp, ifaceFileDef *scope, overDef *od);
static void generateNamedBaseType(ifaceFileDef *, argDef *, const char *, int,
        int, FILE *);
static void generateTupleBuilder(moduleDef *, signatureDef *, FILE *);
static int generatePyQtEmitters(classDef *cd, FILE *fp);
static int generateVirtualHandler(moduleDef *mod, virtHandlerDef *vhd,
        FILE *fp);
static int generateDefaultInstanceReturn(argDef *res, const char *indent,
        FILE *fp);
static int generateVirtualCatcher(moduleDef *mod, classDef *cd, int virtNr,
        virtOverDef *vod, FILE *fp);
static void generateVirtHandlerCall(moduleDef *mod, classDef *cd,
        virtOverDef *vod, argDef *res, const char *indent, FILE *fp);
static void generateProtectedEnums(sipSpec *, classDef *, FILE *);
static void generateProtectedDeclarations(classDef *, FILE *);
static void generateProtectedDefinitions(moduleDef *, classDef *, FILE *);
static void generateProtectedCallArgs(moduleDef *mod, signatureDef *sd,
        FILE *fp);
static void generateConstructorCall(classDef *, ctorDef *, int, int,
        moduleDef *, FILE *);
static void generateHandleResult(moduleDef *, overDef *, int, int, char *,
        FILE *);
static int generateOrdinaryFunction(sipSpec *pt, moduleDef *mod,
        classDef *c_scope, mappedTypeDef *mt_scope, memberDef *md, FILE *fp);
static void generateSimpleFunctionCall(fcallDef *, int, FILE *);
static int generateResultVar(ifaceFileDef *scope, overDef *od, argDef *res,
        const char *indent, FILE *fp);
static void generateFunctionCall(classDef *c_scope, mappedTypeDef *mt_scope,
        ifaceFileDef *o_scope, overDef *od, int deref, moduleDef *mod,
        FILE *fp);
static void generateCppFunctionCall(moduleDef *mod, ifaceFileDef *scope,
        ifaceFileDef *o_scope, overDef *od, FILE *fp);
static void generateSlotArg(moduleDef *mod, signatureDef *sd, int argnr,
        FILE *fp);
static void generateComparisonSlotCall(moduleDef *mod, ifaceFileDef *scope,
        overDef *od, const char *op, const char *cop, int deref, FILE *fp);
static void generateBinarySlotCall(moduleDef *mod, ifaceFileDef *scope,
        overDef *od, const char *op, int deref, FILE *fp);
static void generateNumberSlotCall(moduleDef *mod, overDef *od, char *op,
        FILE *fp);
static void generateVariableGetter(ifaceFileDef *, varDef *, FILE *);
static void generateVariableSetter(ifaceFileDef *, varDef *, FILE *);
static void generateObjToCppConversion(argDef *ad, int has_state, FILE *fp);
static void generateVarMember(varDef *vd, FILE *fp);
static int generateVoidPointers(sipSpec *pt, moduleDef *mod, classDef *cd,
        FILE *fp);
static int generateChars(sipSpec *pt, moduleDef *mod, classDef *cd, FILE *fp);
static int generateStrings(sipSpec *pt, moduleDef *mod, classDef *cd, FILE *fp);
static sortedMethTab *createFunctionTable(memberDef *, int *);
static sortedMethTab *createMethodTable(classDef *, int *);
static int generateMappedTypeMethodTable(sipSpec *pt, mappedTypeDef *mtd,
        FILE *fp);
static int generateClassMethodTable(sipSpec *pt, classDef *cd, FILE *fp);
static void prMethodTable(sipSpec *pt, sortedMethTab *mtable, int nr,
        ifaceFileDef *iff, overDef *overs, FILE *fp);
static void generateEnumMacros(sipSpec *pt, moduleDef *mod, classDef *cd,
        mappedTypeDef *mtd, moduleDef *imported_module, FILE *fp);
static int generateEnumMemberTable(sipSpec *pt, moduleDef *mod, classDef *cd,
        mappedTypeDef *mtd, FILE *fp);
static int generateInts(sipSpec *pt, moduleDef *mod, ifaceFileDef *iff,
        FILE *fp);
static int generateLongs(sipSpec *pt, moduleDef *mod, classDef *cd, FILE *fp);
static int generateUnsignedLongs(sipSpec *pt, moduleDef *mod, classDef *cd,
        FILE *fp);
static int generateLongLongs(sipSpec *pt, moduleDef *mod, classDef *cd,
        FILE *fp);
static int generateUnsignedLongLongs(sipSpec *pt, moduleDef *mod, classDef *cd,
        FILE *fp);
static int generateVariableType(sipSpec *pt, moduleDef *mod, classDef *cd,
        argType atype, const char *eng, const char *s1, const char *s2,
        FILE *fp);
static int generateDoubles(sipSpec *pt, moduleDef *mod, classDef *cd, FILE *fp);
static int generateClasses(sipSpec *pt, moduleDef *mod, classDef *cd, FILE *fp);
static void generateTypesInline(sipSpec *pt, moduleDef *mod, FILE *fp);
static void generateAccessFunctions(sipSpec *pt, moduleDef *mod, classDef *cd,
        FILE *fp);
static void generateConvertToDefinitions(mappedTypeDef *, classDef *, FILE *);
static void generateEncodedType(moduleDef *mod, classDef *cd, int last,
        FILE *fp);
static int generateArgParser(moduleDef *mod, signatureDef *sd,
        classDef *c_scope, mappedTypeDef *mt_scope, ctorDef *ct, overDef *od,
        FILE *fp);
static void generateTry(throwArgs *, FILE *);
static void generateCatch(throwArgs *ta, signatureDef *sd, moduleDef *mod,
        FILE *fp, int rgil);
static void generateCatchBlock(moduleDef *mod, exceptionDef *xd,
        signatureDef *sd, FILE *fp, int rgil);
static void generateThrowSpecifier(throwArgs *, FILE *);
static int generateSlot(moduleDef *mod, classDef *cd, enumDef *ed,
        memberDef *md, FILE *fp);
static void generateCastZero(argDef *ad, FILE *fp);
static void generateCallDefaultCtor(ctorDef *ct, FILE *fp);
static void generateVoidPtrCast(argDef *ad, FILE *fp);
static int countVirtuals(classDef *);
static int skipOverload(overDef *, memberDef *, classDef *, classDef *, int);
static int compareMethTab(const void *, const void *);
static int compareEnumMembers(const void *, const void *);
static char *getSubFormatChar(char, argDef *);
static char *createIfaceFileName(const char *, ifaceFileDef *, const char *);
static FILE *createCompilationUnit(moduleDef *mod, stringList **generated,
        const char *fname, const char *description);
static FILE *createFile(moduleDef *mod, const char *fname,
        const char *description);
static int closeFile(FILE *);
static void prScopedName(FILE *fp, scopedNameDef *snd, char *sep);
static void prTypeName(FILE *fp, argDef *ad);
static void prScopedClassName(FILE *fp, ifaceFileDef *scope, classDef *cd,
        int strip);
static int isMultiArgSlot(memberDef *md);
static int isIntArgSlot(memberDef *md);
static int isInplaceSequenceSlot(memberDef *md);
static int needErrorFlag(codeBlockList *cbl);
static int needOldErrorFlag(codeBlockList *cbl);
static int needNewInstance(argDef *ad);
static int needDealloc(classDef *cd);
static const char *getBuildResultFormat(argDef *ad);
static const char *getParseResultFormat(argDef *ad, int res_isref, int xfervh);
static void generateParseResultExtraArgs(moduleDef *mod, argDef *ad, int argnr,
        FILE *fp);
static char *makePartName(const char *codeDir, const char *mname, int part,
        const char *srcSuffix);
static void fakeProtectedArgs(signatureDef *sd);
static const char *slotName(slotType st);
static void ints_intro(ifaceFileDef *iff, FILE *fp);
static const char *argName(const char *name, codeBlockList *cbl);
static void generateDefaultValue(moduleDef *mod, argDef *ad, int argnr,
        FILE *fp);
static void generateClassFromVoid(classDef *cd, const char *cname,
        const char *vname, FILE *fp);
static void generateMappedTypeFromVoid(mappedTypeDef *mtd, const char *cname,
        const char *vname, FILE *fp);
static int generateSubClassConvertors(sipSpec *pt, moduleDef *mod, FILE *fp);
static void generateNameCache(sipSpec *pt, FILE *fp);
static const char *resultOwner(overDef *od);
static void prCachedName(FILE *fp, nameDef *nd, const char *prefix);
static void generateSignalTableEntry(sipSpec *pt, classDef *cd, overDef *sig,
        int membernr, int optional_args, FILE *fp);
static void generateTypesTable(moduleDef *mod, FILE *fp);
static int keepPyReference(argDef *ad);
static int isDuplicateProtected(classDef *cd, overDef *target);
static char getEncoding(argDef *ad);
static void generateTypeDefName(ifaceFileDef *iff, FILE *fp);
static int hasMemberDocstring(sipSpec *pt, overDef *overs, memberDef *md);
static int generateMemberDocstring(sipSpec *pt, overDef *overs, memberDef *md,
        int is_method, FILE *fp);
static void generateMemberAutoDocstring(sipSpec *pt, overDef *od,
        int is_method, FILE *fp);
static int hasClassDocstring(sipSpec *pt, classDef *cd);
static void generateClassDocstring(sipSpec *pt, classDef *cd, FILE *fp);
static void generateCtorAutoDocstring(sipSpec *pt, classDef *cd, ctorDef *ct,
        FILE *fp);
static void generateDocstringText(docstringDef *docstring, FILE *fp);
static int needsHeapCopy(argDef *ad, int usingCopyCtor);
static void generatePreprocLine(int linenr, const char *fname, FILE *fp);
static int hasOptionalArgs(overDef *od);
static int emptyIfaceFile(sipSpec *pt, ifaceFileDef *iff);
static void declareLimitedAPI(int py_debug, moduleDef *mod, FILE *fp);
static int generatePluginSignalsTable(sipSpec *pt, classDef *cd, FILE *fp);
static int generatePyQt6MappedTypePlugin(sipSpec *pt, mappedTypeDef *mtd,
        FILE *fp);
static int generatePyQtClassPlugin(sipSpec *pt, classDef *cd, FILE *fp);
static void generateGlobalFunctionTableEntries(sipSpec *pt, moduleDef *mod,
        memberDef *members, FILE *fp);
static void prTemplateType(FILE *fp, ifaceFileDef *scope, templateDef *td,
        int strip);
static int isString(argDef *ad);
static scopedNameDef *stripScope(scopedNameDef *snd, int strip);
static void prEnumMemberScope(enumMemberDef *emd, FILE *fp);
static void normaliseSignalArg(argDef *ad);
static void generate_include_sip_h(moduleDef *mod, FILE *fp);
static int get_nr_members(const enumDef *ed);
ifaceFileDef *pyScopeIface(classDef *cd);
ifaceFileDef *pyEnumScopeIface(enumDef *ed);
static void generateEnumMember(FILE *fp, enumMemberDef *emd,
        mappedTypeDef *mtd);
static int typeNeedsUserState(argDef *ad);
static const char *userStateSuffix(argDef *ad);
static void generateExceptionHandler(sipSpec *pt, moduleDef *mod, FILE *fp);
static void normaliseArg(argDef *ad);
static void restoreArg(argDef *ad);
static int stringFind(stringList *sl, const char *s);
static scopedNameDef *getFQCNameOfType(argDef *ad);
static void dsOverload(sipSpec *pt, overDef *od, int is_method, FILE *fp);
static int selectedQualifier(stringList *needed_qualifiers, qualDef *qd);
static int excludedFeature(stringList *xsl, qualDef *qd);
static int sameSignature(signatureDef *sd1, signatureDef *sd2, int strict);
static int sameArgType(argDef *a1, argDef *a2, int strict);
static int sameBaseType(argDef *a1, argDef *a2);
static void prOverloadName(FILE *fp, overDef *od);
static int isZeroArgSlot(memberDef *md);
static int isIntReturnSlot(memberDef *md);
static int isSSizeReturnSlot(memberDef *md);
static int isHashReturnSlot(memberDef *md);
static int isVoidReturnSlot(memberDef *md);
static int isInplaceNumberSlot(memberDef *md);
static int isRichCompareSlot(memberDef *md);
static int usedInCode(codeBlockList *cbl, const char *str);
static void errorScopedName(scopedNameDef *snd);
static const char *get_argument_name(argDef *arg, int arg_nr,
        moduleDef *module);
static void generateSequenceSupport(classDef *klass, overDef *overload,
        moduleDef *module, FILE *fp);


/*
 * Generate the code from a specification and return a list of generated files.
 */
stringList *generateCode(sipSpec *pt, char *codeDir, const char *srcSuffix,
        int except, int trace, int releaseGIL, int parts,
        stringList *needed_qualifiers, stringList *xsl, int docs, int py_debug,
        const char **api_header)
{
    stringList *generated = NULL;

    exceptions = except;
    tracing = trace;
    release_gil = releaseGIL;
    generating_c = pt->genc;
    docstrings = docs;

    if (srcSuffix == NULL)
        srcSuffix = (generating_c ? ".c" : ".cpp");

    if (pt->is_composite)
    {
        if (generateCompositeCpp(pt, codeDir, &generated, py_debug) < 0)
            return NULL;

        *api_header = NULL;
    }
    else
    {
        *api_header = generateCpp(pt, pt->module, codeDir, &generated,
                srcSuffix, parts, needed_qualifiers, xsl, py_debug);

        if (*api_header == NULL)
            return NULL;
    }

    return generated;
}


/*
 * Generate an expression in C++.
 */
void generateExpression(valueDef *vd, int in_str, FILE *fp)
{
    while (vd != NULL)
    {
        if (vd->cast != NULL)
            prcode(fp, "(%S)", vd->cast);

        if (vd->vunop != '\0')
            prcode(fp,"%c",vd->vunop);

        switch (vd->vtype)
        {
        case qchar_value:
            if (vd->u.vqchar == '"' && in_str)
                prcode(fp, "'\\\"'");
            else
                prcode(fp, "'%c'", vd->u.vqchar);

            break;

        case string_value:
            {
                const char *cp, *quote = (in_str ? "\\\"" : "\"");

                prcode(fp, "%s", quote);

                for (cp = vd->u.vstr; *cp != '\0'; ++cp)
                {
                    char ch = *cp;
                    int escape;

                    if (strchr("\\\"", ch) != NULL)
                    {
                        escape = TRUE;
                    }
                    else if (ch == '\n')
                    {
                        escape = TRUE;
                        ch = 'n';
                    }
                    else if (ch == '\r')
                    {
                        escape = TRUE;
                        ch = 'r';
                    }
                    else if (ch == '\t')
                    {
                        escape = TRUE;
                        ch = 't';
                    }
                    else
                    {
                        escape = FALSE;
                    }

                    prcode(fp, "%s%c", (escape ? "\\" : ""), ch);
                }

                prcode(fp, "%s", quote);
            }

            break;

        case numeric_value:
            prcode(fp,"%l",vd->u.vnum);
            break;

        case real_value:
            prcode(fp,"%g",vd->u.vreal);
            break;

        case scoped_value:
            if (prcode_xml)
                prScopedName(fp, removeGlobalScope(vd->u.vscp), ".");
            else
                prcode(fp, "%S", vd->u.vscp);

            break;

        case fcall_value:
            generateSimpleFunctionCall(vd->u.fcd, in_str, fp);
            break;

        case empty_value:
            prcode(fp, "{}");
            break;
        }
 
        if (vd->vbinop != '\0')
            prcode(fp,"%c",vd->vbinop);
 
        vd = vd->next;
    }
}


/*
 * Generate the #defines for each feature defined in a module.
 */
static int generateFeatureDefines(moduleDef *mod,
        stringList *needed_qualifiers, stringList *xsl, int noIntro, FILE *fp)
{
    qualDef *qd;

    for (qd = mod->qualifiers; qd != NULL; qd = qd->next)
    {
        const char *qtype = NULL;

        switch (qd->qtype)
        {
        case time_qualifier:
            if (selectedQualifier(needed_qualifiers, qd))
                qtype = "TIMELINE";

            break;

        case platform_qualifier:
            if (selectedQualifier(needed_qualifiers, qd))
                qtype = "PLATFORM";

            break;

        case feature_qualifier:
            if (!excludedFeature(xsl, qd))
                qtype = "FEATURE";

            break;
        }

        if (qtype != NULL)
        {
            if (noIntro)
            {
                prcode(fp,
"\n"
"/* These are the qualifiers that are enabled. */\n"
                    );

                noIntro = FALSE;
            }

            prcode(fp,
"#define SIP_%s_%s\n"
                , qtype, qd->name);
        }
    }

    return noIntro;
}


/*
 * Generate the C++ internal module API header file and return its path name on
 * the heap.
 */
static const char *generateInternalAPIHeader(sipSpec *pt, moduleDef *mod,
        const char *codeDir, stringList *needed_qualifiers, stringList *xsl,
        int py_debug)
{
    char *hfile;
    const char *mname = mod->name;
    int noIntro;
    FILE *fp;
    nameDef *nd;
    moduleListDef *mld;

    hfile = concat(codeDir, "/sipAPI", mname, ".h", NULL);
    fp = createFile(mod, hfile, "Internal module API header file.");

    if (fp == NULL)
        return NULL;

    /* Include files. */

    prcode(fp,
"\n"
"#ifndef _%sAPI_H\n"
"#define _%sAPI_H\n"
        , mname
        , mname);

    declareLimitedAPI(py_debug, mod, fp);

    generate_include_sip_h(mod, fp);

    if (pluginPyQt5(pt) || pluginPyQt6(pt))
        prcode(fp,
"\n"
"#include <QMetaType>\n"
"#include <QThread>\n"
            );

    /* Define the qualifiers. */
    noIntro = generateFeatureDefines(mod, needed_qualifiers, xsl, TRUE, fp);

    for (mld = mod->allimports; mld != NULL; mld = mld->next)
        noIntro = generateFeatureDefines(mld->module, needed_qualifiers, xsl,
                noIntro, fp);

    if (!noIntro)
        prcode(fp,
"\n"
            );

    /* Shortcuts that hide the messy detail of the APIs. */
    noIntro = TRUE;

    for (nd = pt->namecache; nd != NULL; nd = nd->next)
    {
        if (!isUsedName(nd))
            continue;

        if (noIntro)
        {
            prcode(fp,
"\n"
"/*\n"
" * Convenient names to refer to various strings defined in this module.\n"
" * Only the class names are part of the public API.\n"
" */\n"
                );

            noIntro = FALSE;
        }

        prcode(fp,
"#define %n %d\n"
"#define %N &sipStrings_%s[%d]\n"
            , nd, (int)nd->offset
            , nd, pt->module->name, (int)nd->offset);
    }

    /* These are common to all ABI versions. */
    prcode(fp,
"\n"
"#define sipMalloc                   sipAPI_%s->api_malloc\n"
"#define sipFree                     sipAPI_%s->api_free\n"
"#define sipBuildResult              sipAPI_%s->api_build_result\n"
"#define sipCallMethod               sipAPI_%s->api_call_method\n"
"#define sipCallProcedureMethod      sipAPI_%s->api_call_procedure_method\n"
"#define sipCallErrorHandler         sipAPI_%s->api_call_error_handler\n"
"#define sipParseResultEx            sipAPI_%s->api_parse_result_ex\n"
"#define sipParseResult              sipAPI_%s->api_parse_result\n"
"#define sipParseArgs                sipAPI_%s->api_parse_args\n"
"#define sipParseKwdArgs             sipAPI_%s->api_parse_kwd_args\n"
"#define sipParsePair                sipAPI_%s->api_parse_pair\n"
"#define sipInstanceDestroyed        sipAPI_%s->api_instance_destroyed\n"
"#define sipInstanceDestroyedEx      sipAPI_%s->api_instance_destroyed_ex\n"
"#define sipConvertFromSequenceIndex sipAPI_%s->api_convert_from_sequence_index\n"
"#define sipConvertFromSliceObject   sipAPI_%s->api_convert_from_slice_object\n"
"#define sipConvertFromVoidPtr       sipAPI_%s->api_convert_from_void_ptr\n"
"#define sipConvertToVoidPtr         sipAPI_%s->api_convert_to_void_ptr\n"
"#define sipAddException             sipAPI_%s->api_add_exception\n"
"#define sipNoFunction               sipAPI_%s->api_no_function\n"
"#define sipNoMethod                 sipAPI_%s->api_no_method\n"
"#define sipAbstractMethod           sipAPI_%s->api_abstract_method\n"
"#define sipBadClass                 sipAPI_%s->api_bad_class\n"
"#define sipBadCatcherResult         sipAPI_%s->api_bad_catcher_result\n"
"#define sipBadCallableArg           sipAPI_%s->api_bad_callable_arg\n"
"#define sipBadOperatorArg           sipAPI_%s->api_bad_operator_arg\n"
"#define sipTrace                    sipAPI_%s->api_trace\n"
"#define sipTransferBack             sipAPI_%s->api_transfer_back\n"
"#define sipTransferTo               sipAPI_%s->api_transfer_to\n"
"#define sipSimpleWrapper_Type       sipAPI_%s->api_simplewrapper_type\n"
"#define sipWrapper_Type             sipAPI_%s->api_wrapper_type\n"
"#define sipWrapperType_Type         sipAPI_%s->api_wrappertype_type\n"
"#define sipVoidPtr_Type             sipAPI_%s->api_voidptr_type\n"
"#define sipGetPyObject              sipAPI_%s->api_get_pyobject\n"
"#define sipGetAddress               sipAPI_%s->api_get_address\n"
"#define sipGetMixinAddress          sipAPI_%s->api_get_mixin_address\n"
"#define sipGetCppPtr                sipAPI_%s->api_get_cpp_ptr\n"
"#define sipGetComplexCppPtr         sipAPI_%s->api_get_complex_cpp_ptr\n"
"#define sipCallHook                 sipAPI_%s->api_call_hook\n"
"#define sipEndThread                sipAPI_%s->api_end_thread\n"
"#define sipRaiseUnknownException    sipAPI_%s->api_raise_unknown_exception\n"
"#define sipRaiseTypeException       sipAPI_%s->api_raise_type_exception\n"
"#define sipBadLengthForSlice        sipAPI_%s->api_bad_length_for_slice\n"
"#define sipAddTypeInstance          sipAPI_%s->api_add_type_instance\n"
"#define sipPySlotExtend             sipAPI_%s->api_pyslot_extend\n"
"#define sipAddDelayedDtor           sipAPI_%s->api_add_delayed_dtor\n"
"#define sipCanConvertToType         sipAPI_%s->api_can_convert_to_type\n"
"#define sipConvertToType            sipAPI_%s->api_convert_to_type\n"
"#define sipForceConvertToType       sipAPI_%s->api_force_convert_to_type\n"
"#define sipConvertToEnum            sipAPI_%s->api_convert_to_enum\n"
"#define sipConvertToBool            sipAPI_%s->api_convert_to_bool\n"
"#define sipReleaseType              sipAPI_%s->api_release_type\n"
"#define sipConvertFromType          sipAPI_%s->api_convert_from_type\n"
"#define sipConvertFromNewType       sipAPI_%s->api_convert_from_new_type\n"
"#define sipConvertFromNewPyType     sipAPI_%s->api_convert_from_new_pytype\n"
"#define sipConvertFromEnum          sipAPI_%s->api_convert_from_enum\n"
"#define sipGetState                 sipAPI_%s->api_get_state\n"
"#define sipExportSymbol             sipAPI_%s->api_export_symbol\n"
"#define sipImportSymbol             sipAPI_%s->api_import_symbol\n"
"#define sipFindType                 sipAPI_%s->api_find_type\n"
"#define sipBytes_AsChar             sipAPI_%s->api_bytes_as_char\n"
"#define sipBytes_AsString           sipAPI_%s->api_bytes_as_string\n"
"#define sipString_AsASCIIChar       sipAPI_%s->api_string_as_ascii_char\n"
"#define sipString_AsASCIIString     sipAPI_%s->api_string_as_ascii_string\n"
"#define sipString_AsLatin1Char      sipAPI_%s->api_string_as_latin1_char\n"
"#define sipString_AsLatin1String    sipAPI_%s->api_string_as_latin1_string\n"
"#define sipString_AsUTF8Char        sipAPI_%s->api_string_as_utf8_char\n"
"#define sipString_AsUTF8String      sipAPI_%s->api_string_as_utf8_string\n"
"#define sipUnicode_AsWChar          sipAPI_%s->api_unicode_as_wchar\n"
"#define sipUnicode_AsWString        sipAPI_%s->api_unicode_as_wstring\n"
"#define sipConvertFromConstVoidPtr  sipAPI_%s->api_convert_from_const_void_ptr\n"
"#define sipConvertFromVoidPtrAndSize    sipAPI_%s->api_convert_from_void_ptr_and_size\n"
"#define sipConvertFromConstVoidPtrAndSize   sipAPI_%s->api_convert_from_const_void_ptr_and_size\n"
"#define sipWrappedTypeName(wt)      ((wt)->wt_td->td_cname)\n"
"#define sipDeprecated               sipAPI_%s->api_deprecated\n"
"#define sipGetReference             sipAPI_%s->api_get_reference\n"
"#define sipKeepReference            sipAPI_%s->api_keep_reference\n"
"#define sipRegisterProxyResolver    sipAPI_%s->api_register_proxy_resolver\n"
"#define sipRegisterPyType           sipAPI_%s->api_register_py_type\n"
"#define sipTypeFromPyTypeObject     sipAPI_%s->api_type_from_py_type_object\n"
"#define sipTypeScope                sipAPI_%s->api_type_scope\n"
"#define sipResolveTypedef           sipAPI_%s->api_resolve_typedef\n"
"#define sipRegisterAttributeGetter  sipAPI_%s->api_register_attribute_getter\n"
"#define sipEnableAutoconversion     sipAPI_%s->api_enable_autoconversion\n"
"#define sipInitMixin                sipAPI_%s->api_init_mixin\n"
"#define sipExportModule             sipAPI_%s->api_export_module\n"
"#define sipInitModule               sipAPI_%s->api_init_module\n"
"#define sipGetInterpreter           sipAPI_%s->api_get_interpreter\n"
"#define sipSetTypeUserData          sipAPI_%s->api_set_type_user_data\n"
"#define sipGetTypeUserData          sipAPI_%s->api_get_type_user_data\n"
"#define sipPyTypeDict               sipAPI_%s->api_py_type_dict\n"
"#define sipPyTypeName               sipAPI_%s->api_py_type_name\n"
"#define sipGetCFunction             sipAPI_%s->api_get_c_function\n"
"#define sipGetMethod                sipAPI_%s->api_get_method\n"
"#define sipFromMethod               sipAPI_%s->api_from_method\n"
"#define sipGetDate                  sipAPI_%s->api_get_date\n"
"#define sipFromDate                 sipAPI_%s->api_from_date\n"
"#define sipGetDateTime              sipAPI_%s->api_get_datetime\n"
"#define sipFromDateTime             sipAPI_%s->api_from_datetime\n"
"#define sipGetTime                  sipAPI_%s->api_get_time\n"
"#define sipFromTime                 sipAPI_%s->api_from_time\n"
"#define sipIsUserType               sipAPI_%s->api_is_user_type\n"
"#define sipCheckPluginForType       sipAPI_%s->api_check_plugin_for_type\n"
"#define sipUnicodeNew               sipAPI_%s->api_unicode_new\n"
"#define sipUnicodeWrite             sipAPI_%s->api_unicode_write\n"
"#define sipUnicodeData              sipAPI_%s->api_unicode_data\n"
"#define sipGetBufferInfo            sipAPI_%s->api_get_buffer_info\n"
"#define sipReleaseBufferInfo        sipAPI_%s->api_release_buffer_info\n"
"#define sipIsOwnedByPython          sipAPI_%s->api_is_owned_by_python\n"
"#define sipIsDerivedClass           sipAPI_%s->api_is_derived_class\n"
"#define sipGetUserObject            sipAPI_%s->api_get_user_object\n"
"#define sipSetUserObject            sipAPI_%s->api_set_user_object\n"
"#define sipRegisterEventHandler     sipAPI_%s->api_register_event_handler\n"
"#define sipConvertToArray           sipAPI_%s->api_convert_to_array\n"
"#define sipConvertToTypedArray      sipAPI_%s->api_convert_to_typed_array\n"
"#define sipEnableGC                 sipAPI_%s->api_enable_gc\n"
"#define sipPrintObject              sipAPI_%s->api_print_object\n"
"#define sipLong_AsChar              sipAPI_%s->api_long_as_char\n"
"#define sipLong_AsSignedChar        sipAPI_%s->api_long_as_signed_char\n"
"#define sipLong_AsUnsignedChar      sipAPI_%s->api_long_as_unsigned_char\n"
"#define sipLong_AsShort             sipAPI_%s->api_long_as_short\n"
"#define sipLong_AsUnsignedShort     sipAPI_%s->api_long_as_unsigned_short\n"
"#define sipLong_AsInt               sipAPI_%s->api_long_as_int\n"
"#define sipLong_AsUnsignedInt       sipAPI_%s->api_long_as_unsigned_int\n"
"#define sipLong_AsLong              sipAPI_%s->api_long_as_long\n"
"#define sipLong_AsUnsignedLong      sipAPI_%s->api_long_as_unsigned_long\n"
"#define sipLong_AsLongLong          sipAPI_%s->api_long_as_long_long\n"
"#define sipLong_AsUnsignedLongLong  sipAPI_%s->api_long_as_unsigned_long_long\n"
"#define sipLong_AsSizeT             sipAPI_%s->api_long_as_size_t\n"
"#define sipVisitWrappers            sipAPI_%s->api_visit_wrappers\n"
"#define sipRegisterExitNotifier     sipAPI_%s->api_register_exit_notifier\n"
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname
        ,mname);

    /* These are dependent on the specific ABI version. */
    if (abiVersion >= ABI_13_0)
    {
        /* ABI v13.6 and later. */
        if (abiVersion >= ABI_13_6)
            prcode(fp,
"#define sipPyTypeDictRef            sipAPI_%s->api_py_type_dict_ref\n"
                , mname);

        /* ABI v13.1 and later. */
        if (abiVersion >= ABI_13_1)
            prcode(fp,
"#define sipNextExceptionHandler     sipAPI_%s->api_next_exception_handler\n"
                , mname);

        /* ABI v13.0 and later. */
        prcode(fp,
"#define sipIsEnumFlag               sipAPI_%s->api_is_enum_flag\n"
"#define sipConvertToTypeUS          sipAPI_%s->api_convert_to_type_us\n"
"#define sipForceConvertToTypeUS     sipAPI_%s->api_force_convert_to_type_us\n"
"#define sipReleaseTypeUS            sipAPI_%s->api_release_type_us\n"
            , mname
            , mname
            , mname
            , mname);
    }
    else
    {
        /* ABI v12.13 and later. */
        if (abiVersion >= ABI_12_13)
            prcode(fp,
"#define sipPyTypeDictRef            sipAPI_%s->api_py_type_dict_ref\n"
                , mname);

        /* ABI v12.9 and later. */
        if (abiVersion >= ABI_12_9)
            prcode(fp,
"#define sipNextExceptionHandler     sipAPI_%s->api_next_exception_handler\n"
                , mname);

        /* ABI v12.8 and earlier. */
        prcode(fp,
"#define sipSetNewUserTypeHandler    sipAPI_%s->api_set_new_user_type_handler\n"
"#define sipGetFrame                 sipAPI_%s->api_get_frame\n"
"#define sipSetDestroyOnExit         sipAPI_%s->api_set_destroy_on_exit\n"
"#define sipEnableOverflowChecking   sipAPI_%s->api_enable_overflow_checking\n"
"#define sipIsAPIEnabled             sipAPI_%s->api_is_api_enabled\n"
"#define sipClearAnySlotReference    sipAPI_%s->api_clear_any_slot_reference\n"
"#define sipConnectRx                sipAPI_%s->api_connect_rx\n"
"#define sipConvertRx                sipAPI_%s->api_convert_rx\n"
"#define sipDisconnectRx             sipAPI_%s->api_disconnect_rx\n"
"#define sipFreeSipslot              sipAPI_%s->api_free_sipslot\n"
"#define sipInvokeSlot               sipAPI_%s->api_invoke_slot\n"
"#define sipInvokeSlotEx             sipAPI_%s->api_invoke_slot_ex\n"
"#define sipSameSlot                 sipAPI_%s->api_same_slot\n"
"#define sipSaveSlot                 sipAPI_%s->api_save_slot\n"
"#define sipVisitSlot                sipAPI_%s->api_visit_slot\n"
            , mname
            , mname
            , mname
            , mname
            , mname
            , mname
            , mname
            , mname
            , mname
            , mname
            , mname
            , mname
            , mname
            , mname
            , mname);
    }

    if (abiVersion >= ABI_12_8)
    {
        /* ABI v12.8 and later. */
        prcode(fp,
"#define sipIsPyMethod               sipAPI_%s->api_is_py_method_12_8\n"
            , mname);
    }
    else
    {
        /* ABI v12.7 and earlier. */
        prcode(fp,
"#define sipIsPyMethod               sipAPI_%s->api_is_py_method\n"
            , mname);
    }

    /* The name strings. */
    prcode(fp,
"\n"
"/* The strings used by this module. */\n"
"extern const char sipStrings_%s[];\n"
        , pt->module->name);

    generateModuleAPI(pt, mod, fp);

    prcode(fp,
"\n"
"/* The SIP API, this module's API and the APIs of any imported modules. */\n"
"extern const sipAPIDef *sipAPI_%s;\n"
"extern sipExportedModuleDef sipModuleAPI_%s;\n"
        , mname
        , mname);

    if (mod->nr_needed_types > 0)
        prcode(fp,
"extern sipTypeDef *sipExportedTypes_%s[];\n"
            , mname);

    for (mld = mod->allimports; mld != NULL; mld = mld->next)
    {
        generateImportedModuleAPI(pt, mod, mld->module, fp);

        if (mld->module->nr_needed_types > 0)
            prcode(fp,
"extern sipImportedTypeDef sipImportedTypes_%s_%s[];\n"
                , mname, mld->module->name);

        if (mld->module->nrvirterrorhandlers > 0)
            prcode(fp,
"extern sipImportedVirtErrorHandlerDef sipImportedVirtErrorHandlers_%s_%s[];\n"
                , mname, mld->module->name);

        if (mld->module->nrexceptions > 0)
            prcode(fp,
"extern sipImportedExceptionDef sipImportedExceptions_%s_%s[];\n"
                , mname, mld->module->name);
    }

    if (pluginPyQt5(pt) || pluginPyQt6(pt))
    {
        prcode(fp,
"\n"
"typedef const QMetaObject *(*sip_qt_metaobject_func)(sipSimpleWrapper *, sipTypeDef *);\n"
"extern sip_qt_metaobject_func sip_%s_qt_metaobject;\n"
"\n"
"typedef int (*sip_qt_metacall_func)(sipSimpleWrapper *, sipTypeDef *, QMetaObject::Call, int, void **);\n"
"extern sip_qt_metacall_func sip_%s_qt_metacall;\n"
"\n"
"typedef bool (*sip_qt_metacast_func)(sipSimpleWrapper *, const sipTypeDef *, const char *, void **);\n"
"extern sip_qt_metacast_func sip_%s_qt_metacast;\n"
            , mname
            , mname
            , mname);
    }

    /* Handwritten code. */
    generateCppCodeBlock(pt->exphdrcode, fp);
    generateCppCodeBlock(mod->hdrcode, fp);

    /*
     * Make sure any header code needed by the default exception is included.
     */
    if (mod->defexception != NULL)
        generateCppCodeBlock(mod->defexception->iff->hdrcode, fp);

    /*
     * Note that we don't forward declare the virtual handlers.  This is
     * because we would need to #include everything needed for their argument
     * types.
     */
    prcode(fp,
"\n"
"#endif\n"
        );

    if (closeFile(fp) < 0)
        return NULL;

    return hfile;
}


/*
 * Return the filename of a source code part on the heap.
 */
static char *makePartName(const char *codeDir, const char *mname, int part,
        const char *srcSuffix)
{
    char buf[50];

    sprintf(buf, "part%d", part);

    return concat(codeDir, "/sip", mname, buf, srcSuffix, NULL);
}


/*
 * Generate the C code for a composite module.
 */
static int generateCompositeCpp(sipSpec *pt, const char *codeDir,
        stringList **generated, int py_debug)
{
    char *cppfile;
    moduleListDef *mld;
    FILE *fp;

    cppfile = concat(codeDir, "/sip", pt->module->name, "cmodule.c", NULL);
    fp = createCompilationUnit(pt->module, generated, cppfile,
            "Composite module code.");

    if (fp == NULL)
        return -1;

    prcode(fp,
"\n"
        );

    declareLimitedAPI(py_debug, NULL, fp);

    generate_include_sip_h(pt->module, fp);

    prcode(fp,
"\n"
"\n"
"static void sip_import_component_module(PyObject *d, const char *name)\n"
"{\n"
"    PyObject *mod;\n"
"\n"
"    PyErr_Clear();\n"
"\n"
"    mod = PyImport_ImportModule(name);\n"
"\n"
"    /*\n"
"     * Note that we don't complain if the module can't be imported.  This\n"
"     * is a favour to Linux distro packagers who like to split PyQt into\n"
"     * different sub-packages.\n"
"     */\n"
"    if (mod)\n"
"    {\n"
"        PyDict_Merge(d, PyModule_GetDict(mod), 0);\n"
"        Py_DECREF(mod);\n"
"    }\n"
"}\n"
        );

    generateModDocstring(pt->module, fp);
    generateModInitStart(pt->module, TRUE, fp);
    generateModDefinition(pt->module, "SIP_NULLPTR", fp);

    prcode(fp,
"\n"
"    PyObject *sipModule, *sipModuleDict;\n"
"\n"
"    if ((sipModule = PyModule_Create(&sip_module_def)) == SIP_NULLPTR)\n"
"        return SIP_NULLPTR;\n"
"\n"
"    sipModuleDict = PyModule_GetDict(sipModule);\n"
"\n"
        );

    for (mld = pt->module->allimports; mld != NULL; mld = mld->next)
        prcode(fp,
"    sip_import_component_module(sipModuleDict, \"%s\");\n"
            , mld->module->fullname->text);

    prcode(fp,
"\n"
"    PyErr_Clear();\n"
"\n"
"    return sipModule;\n"
"}\n"
        );

    if (closeFile(fp) < 0)
        return -1;

    free(cppfile);

    return 0;
}


/*
 * Generate the name cache definition.
 */
static void generateNameCache(sipSpec *pt, FILE *fp)
{
    nameDef *nd;

    prcode(fp,
"\n"
"/* Define the strings used by this module. */\n"
"const char sipStrings_%s[] = {\n"
        , pt->module->name);

    for (nd = pt->namecache; nd != NULL; nd = nd->next)
    {
        const char *cp;

        if (!isUsedName(nd) || isSubstring(nd))
            continue;

        prcode(fp, "    ");

        for (cp = nd->text; *cp != '\0'; ++cp)
            prcode(fp, "'%c', ", *cp);

        prcode(fp, "0,\n");
    }

    prcode(fp, "};\n");
}


/*
 * Generate the C/C++ code.
 */
static const char *generateCpp(sipSpec *pt, moduleDef *mod,
        const char *codeDir, stringList **generated, const char *srcSuffix,
        int parts, stringList *needed_qualifiers, stringList *xsl,
        int py_debug)
{
    char *cppfile;
    const char *mname = mod->name;
    int i, nrSccs = 0, files_in_part, max_per_part, this_part, enum_idx;
    int is_inst_class, is_inst_voidp, is_inst_char, is_inst_string;
    int is_inst_int, is_inst_long, is_inst_ulong, is_inst_longlong;
    int is_inst_ulonglong, is_inst_double, nr_enummembers;
    int hasexternal = FALSE, slot_extenders = FALSE, ctor_extenders = FALSE;
    int hasvirterrorhandlers = FALSE;
    FILE *fp;
    moduleListDef *mld;
    classDef *cd;
    memberDef *md;
    enumDef *ed;
    ifaceFileDef *iff;
    virtHandlerDef *vhd;
    virtErrorHandler *veh;
    exceptionDef *xd;
    argDef *ad;

    /* Calculate the number of files in each part. */
    if (parts)
    {
        int nr_files = 1;

        for (iff = pt->ifacefiles; iff != NULL; iff = iff->next)
            if (iff->module == mod && iff->type != exception_iface)
                ++nr_files;

        max_per_part = (nr_files + parts - 1) / parts;
        files_in_part = 1;
        this_part = 0;

        cppfile = makePartName(codeDir, mname, 0, srcSuffix);
    }
    else
        cppfile = concat(codeDir, "/sip", mname, "cmodule", srcSuffix, NULL);

    if ((fp = createCompilationUnit(mod, generated, cppfile, "Module code.")) == NULL)
        return NULL;

    prcode(fp,
"\n"
"#include \"sipAPI%s.h\"\n"
        , mname);

    /*
     * Include the library headers for types used by virtual handlers, module
     * level functions, module level variables, exceptions and Qt meta types.
     */
    generateUsedIncludes(mod->used, fp);

    generateCppCodeBlock(mod->unitpostinccode, fp);

    /*
     * If there should be a Qt support API then generate stubs values for the
     * optional parts.  These should be undefined in %ModuleCode if a C++
     * implementation is provided.
     */
    if (abiVersion < ABI_13_0 && moduleSupportsQt(pt, mod))
        prcode(fp,
"\n"
"#define sipQtCreateUniversalSignal          0\n"
"#define sipQtFindUniversalSignal            0\n"
"#define sipQtEmitSignal                     0\n"
"#define sipQtConnectPySignal                0\n"
"#define sipQtDisconnectPySignal             0\n"
            );

    /* Define the names. */
    generateNameCache(pt, fp);

    /* Generate the C++ code blocks. */
    generateCppCodeBlock(mod->cppcode, fp);

    /* Generate any virtual handlers. */
    for (vhd = pt->virthandlers; vhd != NULL; vhd = vhd->next)
        if (generateVirtualHandler(mod, vhd, fp) < 0)
            return NULL;

    /* Generate any virtual error handlers. */
    for (veh = pt->errorhandlers; veh != NULL; veh = veh->next)
    {
        if (veh->mod == mod)
        {
            prcode(fp,
"\n"
"\n"
"void sipVEH_%s_%s(sipSimpleWrapper *%s, sip_gilstate_t%s)\n"
"{\n"
                , mname, veh->name, (usedInCode(veh->code, "sipPySelf") ? "sipPySelf" : ""), (usedInCode(veh->code, "sipGILState") ? " sipGILState" : ""));

            generateCppCodeBlock(veh->code, fp);

            prcode(fp,
"}\n"
                );
        }
    }

    /* Generate the global functions. */
    for (md = mod->othfuncs; md != NULL; md = md->next)
    {
        if (md->slot == no_slot)
        {
            if (generateOrdinaryFunction(pt, mod, NULL, NULL, md, fp) < 0)
                return NULL;
        }
        else
        {
            overDef *od;

            /*
             * Make sure that there is still an overload and we haven't moved
             * them all to classes.
             */
            for (od = mod->overs; od != NULL; od = od->next)
            {
                if (od->common == md)
                {
                    if (generateSlot(mod, NULL, NULL, md, fp) < 0)
                        return NULL;

                    slot_extenders = TRUE;
                    break;
                }
            }
        }
    }

    /* Generate the global functions for any hidden namespaces. */
    for (cd = pt->classes; cd != NULL; cd = cd->next)
    {
        if (cd->iff->module == mod && isHiddenNamespace(cd))
        {
            for (md = cd->members; md != NULL; md = md->next)
            {
                if (md->slot == no_slot)
                    if (generateOrdinaryFunction(pt, mod, cd, NULL, md, fp) < 0)
                        return NULL;
            }
        }
    }

    /* Generate any class specific ctor or slot extenders. */
    for (cd = mod->proxies; cd != NULL; cd = cd->next)
    {
        if (cd->ctors != NULL)
        {
            if (generateTypeInit(cd, mod, fp) < 0)
                return NULL;

            ctor_extenders = TRUE;
        }

        for (md = cd->members; md != NULL; md = md->next)
        {
            if (generateSlot(mod, cd, NULL, md, fp) < 0)
                return NULL;

            slot_extenders = TRUE;
        }
    }

    /* Generate any ctor extender table. */
    if (ctor_extenders)
    {
        prcode(fp,
"\n"
"static sipInitExtenderDef initExtenders[] = {\n"
            );

        for (cd = mod->proxies; cd != NULL; cd = cd->next)
            if (cd->ctors != NULL)
            {
                if (abiVersion >= ABI_13_0)
                    prcode(fp,
"    {init_type_%L, ", cd->iff);
                else
                    prcode(fp,
"    {-1, init_type_%L, ", cd->iff);

                generateEncodedType(mod, cd, 0, fp);

                prcode(fp, ", SIP_NULLPTR},\n"
                    );
            }

        if (abiVersion >= ABI_13_0)
            prcode(fp,
"    {SIP_NULLPTR, {0, 0, 0}, SIP_NULLPTR}\n"
"};\n"
                );
        else
            prcode(fp,
"    {-1, SIP_NULLPTR, {0, 0, 0}, SIP_NULLPTR}\n"
"};\n"
                );
    }

    /* Generate any slot extender table. */
    if (slot_extenders)
    {
        prcode(fp,
"\n"
"static sipPySlotExtenderDef slotExtenders[] = {\n"
            );

        for (md = mod->othfuncs; md != NULL; md = md->next)
        {
            overDef *od;

            if (md->slot == no_slot)
                continue;

            for (od = mod->overs; od != NULL; od = od->next)
                if (od->common == md)
                {
                    prcode(fp,
"    {(void *)slot_%s, %s, {0, 0, 0}},\n"
                        , md->pyname->text, slotName(md->slot));

                    break;
                }
        }

        for (cd = mod->proxies; cd != NULL; cd = cd->next)
            for (md = cd->members; md != NULL; md = md->next)
            {
                prcode(fp,
"    {(void *)slot_%L_%s, %s, ", cd->iff, md->pyname->text, slotName(md->slot));

                generateEncodedType(mod, cd, 0, fp);

                prcode(fp, "},\n"
                    );
            }

        prcode(fp,
"    {SIP_NULLPTR, (sipPySlotType)0, {0, 0, 0}}\n"
"};\n"
            );
    }

    /* Generate the global access functions. */
    generateAccessFunctions(pt, mod, NULL, fp);

    /* Generate any sub-class convertors. */
    nrSccs = generateSubClassConvertors(pt, mod, fp);

    /* Generate the external classes table if needed. */
    for (cd = pt->classes; cd != NULL; cd = cd->next)
    {
        if (!isExternal(cd))
            continue;

        if (cd->iff->module != mod)
            continue;

        if (!hasexternal)
        {
            prcode(fp,
"\n"
"\n"
"/* This defines each external type declared in this module, */\n"
"static sipExternalTypeDef externalTypesTable[] = {\n"
                );

            hasexternal = TRUE;
        }

        prcode(fp,
"    {%d, \"", cd->iff->ifacenr);
        prScopedName(fp, removeGlobalScope(classFQCName(cd)), ".");
        prcode(fp,"\"},\n"
            );
    }

    if (hasexternal)
        prcode(fp,
"    {-1, SIP_NULLPTR}\n"
"};\n"
            );

    /* Generate any enum slot tables. */
    for (ed = pt->enums; ed != NULL; ed = ed->next)
    {
        memberDef *slot;

        if (ed->module != mod || ed->fqcname == NULL)
            continue;

        if (ed->slots == NULL)
            continue;

        for (slot = ed->slots; slot != NULL; slot = slot->next)
            if (generateSlot(mod, NULL, ed, slot, fp) < 0)
                return NULL;

        prcode(fp,
"\n"
"static sipPySlotDef slots_%C[] = {\n"
            , ed->fqcname);

        for (slot = ed->slots; slot != NULL; slot = slot->next)
        {
            const char *stype;

            if ((stype = slotName(slot->slot)) != NULL)
                prcode(fp,
"    {(void *)slot_%C_%s, %s},\n"
                    , ed->fqcname, slot->pyname->text, stype);
        }

        prcode(fp,
"    {SIP_NULLPTR, (sipPySlotType)0}\n"
"};\n"
"\n"
            );
    }

    /*
     * Generate the enum type structures.  Note that we go through the sorted
     * table of needed types rather than the unsorted list of all enums.
     */
    enum_idx = 0;

    for (ad = mod->needed_types, i = 0; i < mod->nr_needed_types; ++i, ++ad)
    {
        int type_nr = -1;

        if (ad->atype != enum_type)
            continue;

        ed = ad->u.ed;

        if (ed->ecd != NULL)
            type_nr = ed->ecd->iff->ifacenr;
        else if (ed->emtd != NULL)
            type_nr = ed->emtd->iff->ifacenr;

        if (enum_idx == 0)
        {
            prcode(fp,
"static sipEnumTypeDef enumTypes[] = {\n"
                );
        }

        ed->enum_idx = enum_idx++;

        if (abiVersion >= ABI_13_0)
        {
            const char *base_type;

            if (isEnumIntFlag(ed))
                base_type = "SIP_ENUM_INT_FLAG";
            else if (isEnumFlag(ed))
                base_type = "SIP_ENUM_FLAG";
            else if (isEnumIntEnum(ed))
                base_type = "SIP_ENUM_INT_ENUM";
            else if (isEnumUIntEnum(ed))
                base_type = "SIP_ENUM_UINT_ENUM";
            else
                base_type = "SIP_ENUM_ENUM";

            prcode(fp,
"    {{SIP_NULLPTR, SIP_TYPE_ENUM, %n, SIP_NULLPTR, 0}, %s, %n, %d, %d", ed->cname, base_type, ed->pyname, type_nr, get_nr_members(ed));
        }
        else
        {
            prcode(fp,
"    {{-1, SIP_NULLPTR, SIP_NULLPTR, SIP_TYPE_%s, %n, SIP_NULLPTR, 0}, %n, %d", (isScopedEnum(ed) ? "SCOPED_ENUM" : "ENUM"), ed->cname, ed->pyname, type_nr);
        }

        if (ed->slots != NULL)
            prcode(fp, ", slots_%C", ed->fqcname);
        else
            prcode(fp, ", SIP_NULLPTR");

        prcode(fp, "},\n"
            );
    }

    if (enum_idx != 0)
        prcode(fp,
"};\n"
            );

    if (abiVersion >= ABI_13_0)
        nr_enummembers = -1;
    else
        nr_enummembers = generateEnumMemberTable(pt, mod, NULL, NULL, fp);

    /* Generate the types table. */
    if (mod->nr_needed_types > 0)
        generateTypesTable(mod, fp);

    if (mod->nrtypedefs > 0)
    {
        typedefDef *td;

        prcode(fp,
"\n"
"\n"
"/*\n"
" * These define each typedef in this module.\n"
" */\n"
"static sipTypedefDef typedefsTable[] = {\n"
            );

        for (td = pt->typedefs; td != NULL; td = td->next)
        {
            if (td->module != mod)
                continue;

            prcode(fp,
"    {\"%V\", \"", td->fqname);

            /* The default behaviour isn't right in a couple of cases. */
            if (td->type.atype == longlong_type)
                prcode(fp, "long long");
            else if (td->type.atype == ulonglong_type)
                prcode(fp, "unsigned long long");
            else
                generateBaseType(NULL, &td->type, FALSE, STRIP_GLOBAL, fp);

            prcode(fp, "\"},\n"
                );
        }

        prcode(fp,
"};\n"
            );
    }

    for (veh = pt->errorhandlers; veh != NULL; veh = veh->next)
    {
        if (veh->mod == mod)
        {
            if (!hasvirterrorhandlers)
            {
                hasvirterrorhandlers = TRUE;

                prcode(fp,
"\n"
"\n"
"/*\n"
" * This defines the virtual error handlers that this module implements and\n"
" * can be used by other modules.\n"
" */\n"
"static sipVirtErrorHandlerDef virtErrorHandlersTable[] = {\n"
                    );
            }

            prcode(fp,
"    {\"%s\", sipVEH_%s_%s},\n"
                , veh->name, mname, veh->name);
        }
    }

    if (hasvirterrorhandlers)
        prcode(fp,
"    {SIP_NULLPTR, SIP_NULLPTR}\n"
"};\n"
            );

    if (mod->allimports != NULL)
    {
        for (mld = mod->allimports; mld != NULL; mld = mld->next)
        {
            int i;

            if (mld->module->nr_needed_types > 0)
            {
                prcode(fp,
"\n"
"\n"
"/* This defines the types that this module needs to import from %s. */\n"
"sipImportedTypeDef sipImportedTypes_%s_%s[] = {\n"
                    , mld->module->name
                    , mname, mld->module->name);

                for (i = 0; i < mld->module->nr_needed_types; ++i)
                {
                    argDef *ad = &mld->module->needed_types[i];

                    if (ad->atype == mapped_type)
                        prcode(fp,
"    {\"%s\"},\n"
                            , ad->u.mtd->cname->text);
                    else
                        prcode(fp,
"    {\"%V\"},\n"
                            , getFQCNameOfType(ad));
                }

                prcode(fp,
"    {SIP_NULLPTR}\n"
"};\n"
                    );
            }

            if (mld->module->nrvirterrorhandlers > 0)
            {
                int i;

                prcode(fp,
"\n"
"\n"
"/*\n"
" * This defines the virtual error handlers that this module needs to import\n"
" * from %s.\n"
" */\n"
"sipImportedVirtErrorHandlerDef sipImportedVirtErrorHandlers_%s_%s[] = {\n"
                    , mld->module->name
                    , mname, mld->module->name);

                /*
                 * The handlers are unordered so search for each in turn.
                 * There will probably be only one so speed isn't an issue.
                 */
                for (i = 0; i < mld->module->nrvirterrorhandlers; ++i)
                {
                    virtErrorHandler *veh;

                    for (veh = pt->errorhandlers; veh != NULL; veh = veh->next)
                    {
                        if (veh->mod == mld->module && veh->index == i)
                        {
                            prcode(fp,
"    {\"%s\"},\n"
                                , veh->name);
                        }
                    }
                }

                prcode(fp,
"    {SIP_NULLPTR}\n"
"};\n"
                    );
            }

            if (mld->module->nrexceptions > 0)
            {
                int i;

                prcode(fp,
"\n"
"\n"
"/*\n"
" * This defines the exception objects that this module needs to import from\n"
" * %s.\n"
" */\n"
"sipImportedExceptionDef sipImportedExceptions_%s_%s[] = {\n"
                    , mld->module->name
                    , mname, mld->module->name);

                /*
                 * The exceptions are unordered so search for each in turn.
                 * There will probably be very few so speed isn't an issue.
                 */
                for (i = 0; i < mld->module->nrexceptions; ++i)
                {
                    exceptionDef *xd;

                    for (xd = pt->exceptions; xd != NULL; xd = xd->next)
                    {
                        if (xd->iff->module == mld->module && xd->exceptionnr == i)
                        {
                            prcode(fp,
"    {\"%s\"},\n"
                                , xd->pyname);
                        }
                    }
                }

                prcode(fp,
"    {SIP_NULLPTR}\n"
"};\n"
                    );
            }
        }

        prcode(fp,
"\n"
"\n"
"/* This defines the modules that this module needs to import. */\n"
"static sipImportedModuleDef importsTable[] = {\n"
            );

        for (mld = mod->allimports; mld != NULL; mld = mld->next)
        {
            prcode(fp,
"    {\"%s\", ", mld->module->fullname->text);

            if (mld->module->nr_needed_types > 0)
                prcode(fp, "sipImportedTypes_%s_%s, ", mname, mld->module->name);
            else
                prcode(fp, "SIP_NULLPTR, ");

            if (mld->module->nrvirterrorhandlers > 0)
                prcode(fp, "sipImportedVirtErrorHandlers_%s_%s, ", mname, mld->module->name);
            else
                prcode(fp, "SIP_NULLPTR, ");

            if (mld->module->nrexceptions > 0)
                prcode(fp, "sipImportedExceptions_%s_%s", mname, mld->module->name);
            else
                prcode(fp, "SIP_NULLPTR");

            prcode(fp, "},\n"
                );
        }

        prcode(fp,
"    {SIP_NULLPTR, SIP_NULLPTR, SIP_NULLPTR, SIP_NULLPTR}\n"
"};\n"
            );
    }

    if (nrSccs > 0)
    {
        prcode(fp,
"\n"
"\n"
"/* This defines the class sub-convertors that this module defines. */\n"
"static sipSubClassConvertorDef convertorsTable[] = {\n"
            );

        for (cd = pt->classes; cd != NULL; cd = cd->next)
        {
            if (cd->iff->module != mod)
                continue;

            if (cd->convtosubcode == NULL)
                continue;

            prcode(fp,
"    {sipSubClass_%C, ",classFQCName(cd));

            generateEncodedType(mod, cd->subbase, 0, fp);

            prcode(fp,", SIP_NULLPTR},\n");
        }

        prcode(fp,
"    {SIP_NULLPTR, {0, 0, 0}, SIP_NULLPTR}\n"
"};\n"
            );
    }

    /* Generate any license information. */
    if (mod->license != NULL)
    {
        licenseDef *ld = mod->license;

        prcode(fp,
"\n"
"\n"
"/* Define the module's license. */\n"
"static sipLicenseDef module_license = {\n"
            );

        prcode(fp,
"    \"%s\",\n"
            , ld->type);

        if (ld->licensee != NULL)
            prcode(fp,
"    \"%s\",\n"
                , ld->licensee);
        else
            prcode(fp,
"    SIP_NULLPTR,\n"
                );

        if (ld->timestamp != NULL)
            prcode(fp,
"    \"%s\",\n"
                , ld->timestamp);
        else
            prcode(fp,
"    SIP_NULLPTR,\n"
                );

        if (ld->sig != NULL)
            prcode(fp,
"    \"%s\"\n"
                , ld->sig);
        else
            prcode(fp,
"    SIP_NULLPTR\n"
                );

        prcode(fp,
"};\n"
            );
    }

    /* Generate each instance table. */
    is_inst_class = generateClasses(pt, mod, NULL, fp);
    is_inst_voidp = generateVoidPointers(pt, mod, NULL, fp);
    is_inst_char = generateChars(pt, mod, NULL, fp);
    is_inst_string = generateStrings(pt, mod, NULL, fp);
    is_inst_int = generateInts(pt, mod, NULL, fp);
    is_inst_long = generateLongs(pt, mod, NULL, fp);
    is_inst_ulong = generateUnsignedLongs(pt, mod, NULL, fp);
    is_inst_longlong = generateLongLongs(pt, mod, NULL, fp);
    is_inst_ulonglong = generateUnsignedLongLongs(pt, mod, NULL, fp);
    is_inst_double = generateDoubles(pt, mod, NULL, fp);

    /* Generate any exceptions support. */
    if (exceptions)
    {
        if (mod->nrexceptions > 0)
            prcode(fp,
"\n"
"\n"
"PyObject *sipExportedExceptions_%s[%d];\n"
                , mname, mod->nrexceptions + 1);

        if (abiVersion >= ABI_13_1 || (abiVersion >= ABI_12_9 && abiVersion < ABI_13_0))
            generateExceptionHandler(pt, mod, fp);
    }

    /* Generate any Qt support API. */
    if (abiVersion < ABI_13_0 && moduleSupportsQt(pt, mod))
        prcode(fp,
"\n"
"\n"
"/* This defines the Qt support API. */\n"
"\n"
"static sipQtAPI qtAPI = {\n"
"    &sipExportedTypes_%s[%d],\n"
"    sipQtCreateUniversalSignal,\n"
"    sipQtFindUniversalSignal,\n"
"    sipQtCreateUniversalSlot,\n"
"    sipQtDestroyUniversalSlot,\n"
"    sipQtFindSlot,\n"
"    sipQtConnect,\n"
"    sipQtDisconnect,\n"
"    sipQtSameSignalSlotName,\n"
"    sipQtFindSipslot,\n"
"    sipQtEmitSignal,\n"
"    sipQtConnectPySignal,\n"
"    sipQtDisconnectPySignal\n"
"};\n"
            , mname, pt->qobject_cd->iff->ifacenr);

    prcode(fp,
"\n"
"\n"
"/* This defines this module. */\n"
"sipExportedModuleDef sipModuleAPI_%s = {\n"
"    SIP_NULLPTR,\n"
"    %d,\n"
"    %n,\n"
"    0,\n"
"    sipStrings_%s,\n"
"    %s,\n"
        , mname
        , abiVersion & 0xff
        , mod->fullname
        , pt->module->name
        , mod->allimports != NULL ? "importsTable" : "SIP_NULLPTR");

    if (abiVersion < ABI_13_0)
        prcode(fp,
"    %s,\n"
            , moduleSupportsQt(pt, mod) ? "&qtAPI" : "SIP_NULLPTR");

    prcode(fp,
"    %d,\n"
        , mod->nr_needed_types);

    if (mod->nr_needed_types > 0)
        prcode(fp,
"    sipExportedTypes_%s,\n"
            , mname);
    else
        prcode(fp,
"    SIP_NULLPTR,\n"
            );

    prcode(fp,
"    %s,\n"
        , hasexternal ? "externalTypesTable" : "SIP_NULLPTR");

    if (nr_enummembers >= 0)
        prcode(fp,
"    %d,\n"
"    %s,\n"
            , nr_enummembers
            , nr_enummembers > 0 ? "enummembers" : "SIP_NULLPTR");

    prcode(fp,
"    %d,\n"
"    %s,\n"
"    %s,\n"
"    %s,\n"
"    {%s, %s, %s, %s, %s, %s, %s, %s, %s, %s},\n"
"    %s,\n"
        , mod->nrtypedefs
        , mod->nrtypedefs > 0 ? "typedefsTable" : "SIP_NULLPTR"
        , hasvirterrorhandlers ? "virtErrorHandlersTable" : "SIP_NULLPTR"
        , nrSccs > 0 ? "convertorsTable" : "SIP_NULLPTR"
        , is_inst_class ? "typeInstances" : "SIP_NULLPTR"
        , is_inst_voidp ? "voidPtrInstances" : "SIP_NULLPTR"
        , is_inst_char ? "charInstances" : "SIP_NULLPTR"
        , is_inst_string ? "stringInstances" : "SIP_NULLPTR"
        , is_inst_int ? "intInstances" : "SIP_NULLPTR"
        , is_inst_long ? "longInstances" : "SIP_NULLPTR"
        , is_inst_ulong ? "unsignedLongInstances" : "SIP_NULLPTR"
        , is_inst_longlong ? "longLongInstances" : "SIP_NULLPTR"
        , is_inst_ulonglong ? "unsignedLongLongInstances" : "SIP_NULLPTR"
        , is_inst_double ? "doubleInstances" : "SIP_NULLPTR"
        , mod->license != NULL ? "&module_license" : "SIP_NULLPTR");

    if (mod->nrexceptions > 0)
        prcode(fp,
"    sipExportedExceptions_%s,\n"
            , mname);
    else
        prcode(fp,
"    SIP_NULLPTR,\n"
            );

    prcode(fp,
"    %s,\n"
"    %s,\n"
"    %s,\n"
"    SIP_NULLPTR,\n"
        , slot_extenders ? "slotExtenders" : "SIP_NULLPTR"
        , ctor_extenders ? "initExtenders" : "SIP_NULLPTR"
        , hasDelayedDtors(mod) ? "sipDelayedDtors" : "SIP_NULLPTR");

    if (abiVersion < ABI_13_0)
    {
        /* The unused version support. */
        prcode(fp,
"    SIP_NULLPTR,\n"
"    SIP_NULLPTR,\n"
            );
    }

    if (abiVersion >= ABI_13_1 || (abiVersion >= ABI_12_9 && abiVersion < ABI_13_0))
    {
        if (exceptions && mod->nrexceptions > 0)
            prcode(fp,
"    sipExceptionHandler_%s,\n"
                , mname);
        else
            prcode(fp,
"    SIP_NULLPTR,\n"
                );
    }
    else
    {
        prcode(fp,
"    SIP_NULLPTR,\n"
            );
    }

    prcode(fp,
"};\n"
        );

    generateModDocstring(mod, fp);

    /* Generate the storage for the external API pointers. */
    prcode(fp,
"\n"
"\n"
"/* The SIP API and the APIs of any imported modules. */\n"
"const sipAPIDef *sipAPI_%s;\n"
        , mname);

    if (pluginPyQt5(pt) || pluginPyQt6(pt))
        prcode(fp,
"\n"
"sip_qt_metaobject_func sip_%s_qt_metaobject;\n"
"sip_qt_metacall_func sip_%s_qt_metacall;\n"
"sip_qt_metacast_func sip_%s_qt_metacast;\n"
            , mname
            , mname
            , mname);

    /* Generate the Python module initialisation function. */
    generateModInitStart(pt->module, generating_c, fp);

    /* Generate the global functions. */

    prcode(fp,
"    static PyMethodDef sip_methods[] = {\n"
        );

    generateGlobalFunctionTableEntries(pt, mod, mod->othfuncs, fp);

    /* Generate the global functions for any hidden namespaces. */
    for (cd = pt->classes; cd != NULL; cd = cd->next)
        if (cd->iff->module == mod && isHiddenNamespace(cd))
            generateGlobalFunctionTableEntries(pt, mod, cd->members, fp);

    prcode(fp,
"        {SIP_NULLPTR, SIP_NULLPTR, 0, SIP_NULLPTR}\n"
"    };\n"
        );

    generateModDefinition(mod, "sip_methods", fp);

    prcode(fp,
"\n"
"    PyObject *sipModule, *sipModuleDict;\n"
        );

    if (sipName != NULL)
        generateSipImportVariables(fp);

    /* Generate any pre-initialisation code. */
    generateCppCodeBlock(mod->preinitcode, fp);

    prcode(fp,
"    /* Initialise the module and get it's dictionary. */\n"
"    if ((sipModule = PyModule_Create(&sip_module_def)) == SIP_NULLPTR)\n"
"        return SIP_NULLPTR;\n"
"\n"
"    sipModuleDict = PyModule_GetDict(sipModule);\n"
"\n"
        );

    generateSipAPI(mod, fp);

    /* Generate any initialisation code. */
    generateCppCodeBlock(mod->initcode, fp);

    prcode(fp,
"    /* Export the module and publish it's API. */\n"
"    if (sipExportModule(&sipModuleAPI_%s, %d, %d, 0) < 0)\n"
"    {\n"
"        Py_DECREF(sipModule);\n"
"        return SIP_NULLPTR;\n"
"    }\n"
        , mname, abiVersion >> 8, abiVersion & 0xff);

    if (pluginPyQt5(pt) || pluginPyQt6(pt))
    {
        /* Import the helpers. */
        prcode(fp,
"\n"
"    sip_%s_qt_metaobject = (sip_qt_metaobject_func)sipImportSymbol(\"qtcore_qt_metaobject\");\n"
"    sip_%s_qt_metacall = (sip_qt_metacall_func)sipImportSymbol(\"qtcore_qt_metacall\");\n"
"    sip_%s_qt_metacast = (sip_qt_metacast_func)sipImportSymbol(\"qtcore_qt_metacast\");\n"
"\n"
"    if (!sip_%s_qt_metacast)\n"
"        Py_FatalError(\"Unable to import qtcore_qt_metacast\");\n"
"\n"
            , mname
            , mname
            , mname
            , mname);
    }

    prcode(fp,
"    /* Initialise the module now all its dependencies have been set up. */\n"
"    if (sipInitModule(&sipModuleAPI_%s, sipModuleDict) < 0)\n"
"    {\n"
"        Py_DECREF(sipModule);\n"
"        return SIP_NULLPTR;\n"
"    }\n"
        , mname);

    generateTypesInline(pt, mod, fp);

    generatePyObjects(pt, mod, fp);

    /* Create any exception objects. */
    for (xd = pt->exceptions; xd != NULL; xd = xd->next)
    {
        if (xd->iff->module != mod)
            continue;

        if (xd->exceptionnr < 0)
            continue;

        prcode(fp,
"\n"
"    if ((sipExportedExceptions_%s[%d] = PyErr_NewException(\n"
"            \"%s.%s\",\n"
"            "
            , xd->iff->module->name, xd->exceptionnr
            , xd->iff->module->name, xd->pyname);

        if (xd->bibase != NULL)
            prcode(fp, "PyExc_%s", xd->bibase);
        else
            prcode(fp, "sipException_%C", xd->base->iff->fqcname);

        prcode(fp, ", SIP_NULLPTR)) == SIP_NULLPTR || PyDict_SetItemString(sipModuleDict, \"%s\", sipExportedExceptions_%s[%d]) < 0)\n"
"    {\n"
"        Py_DECREF(sipModule);\n"
"        return SIP_NULLPTR;\n"
"    }\n"
            , xd->pyname, xd->iff->module->name, xd->exceptionnr);
    }

    if (mod->nrexceptions > 0)
        prcode(fp,
"\n"
"    sipExportedExceptions_%s[%d] = SIP_NULLPTR;\n"
            , mname, mod->nrexceptions);

    /*
     * Generate the enum meta-type registrations for PyQt6 so that they can be
     * used in queued connections.
     */
    if (pluginPyQt6(pt))
    {
        for (ed = pt->enums; ed != NULL; ed = ed->next)
        {
            if (ed->module != mod || ed->fqcname == NULL)
                continue;

            if (isProtectedEnum(ed))
                continue;

            if (ed->ecd != NULL && noPyQtQMetaObject(ed->ecd))
                continue;

            prcode(fp,
"    qMetaTypeId<%S>();\n"
                , ed->fqcname);
        }
    }

    /* Generate any post-initialisation code. */
    generateCppCodeBlock(mod->postinitcode, fp);

    prcode(fp,
"\n"
"    return sipModule;\n"
"}\n"
        );

    /* Generate the interface source files. */
    for (iff = pt->ifacefiles; iff != NULL; iff = iff->next)
        if (iff->module == mod && iff->type != exception_iface)
        {
            int need_postinc;

            if (parts && files_in_part++ == max_per_part)
            {
                /* Close the old part. */
                if (closeFile(fp) < 0)
                    return NULL;

                free(cppfile);

                /* Create a new one. */
                files_in_part = 1;
                ++this_part;

                cppfile = makePartName(codeDir, mname, this_part, srcSuffix);
                if ((fp = createCompilationUnit(mod, generated, cppfile, "Module code.")) == NULL)
                    return NULL;

                prcode(fp,
"\n"
"#include \"sipAPI%s.h\"\n"
                    , mname);

                need_postinc = TRUE;
            }
            else
            {
                need_postinc = FALSE;
            }

            if (generateIfaceCpp(pt, generated, py_debug, iff, need_postinc, codeDir, srcSuffix, ((parts && iff->file_extension == NULL) ? fp : NULL)) < 0)
                return NULL;
        }

    if (closeFile(fp) < 0)
        return NULL;

    free(cppfile);

    /* How many parts we actually generated. */
    if (parts)
        parts = this_part + 1;

    mod->parts = parts;

    return generateInternalAPIHeader(pt, mod, codeDir, needed_qualifiers, xsl,
            py_debug);
}


/*
 * Generate the types table for a module.
 */
static void generateTypesTable(moduleDef *mod, FILE *fp)
{
    int i;
    argDef *ad;

    prcode(fp,
"\n"
"\n"
"/*\n"
" * This defines each type in this module.\n"
" */\n"
"sipTypeDef *sipExportedTypes_%s[] = {\n"
        , mod->name);

    for (ad = mod->needed_types, i = 0; i < mod->nr_needed_types; ++i, ++ad)
    {
        switch (ad->atype)
        {
        case class_type:
            if (isExternal(ad->u.cd))
                prcode(fp,
"    0,\n"
                    );
            else if (!isHiddenNamespace(ad->u.cd))
                prcode(fp,
"    &sipTypeDef_%s_%L.ctd_base,\n"
                    , mod->name, ad->u.cd->iff);

            break;

        case mapped_type:
            prcode(fp,
"    &sipTypeDef_%s_%L.mtd_base,\n"
                , mod->name, ad->u.mtd->iff);
            break;

        case enum_type:
            prcode(fp,
"    &enumTypes[%d].etd_base,\n"
                , ad->u.ed->enum_idx);
            break;

        /* Supress a compiler warning. */
        default:
            ;
        }
    }

    prcode(fp,
"};\n"
        );
}


/*
 * Generate the code to get the sip API.
 */
static void generateSipAPI(moduleDef *mod, FILE *fp)
{
    /*
     * If there is no sip module name then we are getting the API from a
     * non-shared sip module.
     */
    if (sipName == NULL)
    {
        prcode(fp,
"    if ((sipAPI_%s = sip_init_library(sipModuleDict)) == SIP_NULLPTR)\n"
"        return SIP_NULLPTR;\n"
"\n"
            , mod->name);

        return;
    }

    /*
     * Note that we don't use PyCapsule_Import() because it doesn't handle
     * package.module.attribute.
     */

    prcode(fp,
"    /* Get the SIP module's API. */\n"
"    if ((sip_sipmod = PyImport_ImportModule(\"%s\")) == SIP_NULLPTR)\n"
"    {\n"
"        Py_DECREF(sipModule);\n"
"        return SIP_NULLPTR;\n"
"    }\n"
"\n"
"    sip_capiobj = PyDict_GetItemString(PyModule_GetDict(sip_sipmod), \"_C_API\");\n"
"    Py_DECREF(sip_sipmod);\n"
"\n"
"    if (sip_capiobj == SIP_NULLPTR || !PyCapsule_CheckExact(sip_capiobj))\n"
"    {\n"
"        PyErr_SetString(PyExc_AttributeError, \"%s._C_API is missing or has the wrong type\");\n"
"        Py_DECREF(sipModule);\n"
"        return SIP_NULLPTR;\n"
"    }\n"
"\n"
        , sipName
        , sipName);

    if (generating_c)
        prcode(fp,
"    sipAPI_%s = (const sipAPIDef *)PyCapsule_GetPointer(sip_capiobj, \"%s._C_API\");\n"
        , mod->name, sipName);
    else
        prcode(fp,
"    sipAPI_%s = reinterpret_cast<const sipAPIDef *>(PyCapsule_GetPointer(sip_capiobj, \"%s._C_API\"));\n"
"\n"
        , mod->name, sipName);

    prcode(fp,
"    if (sipAPI_%s == SIP_NULLPTR)\n"
"    {\n"
"        Py_DECREF(sipModule);\n"
"        return SIP_NULLPTR;\n"
"    }\n"
"\n"
        , mod->name);
}


/*
 * Generate the variables needed by generateSipImport().
 */
static void generateSipImportVariables(FILE *fp)
{
    prcode(fp,
"    PyObject *sip_sipmod, *sip_capiobj;\n"
"\n"
        );
}


/*
 * Generate the start of the Python module initialisation function.
 */
static void generateModInitStart(moduleDef *mod, int gen_c, FILE *fp)
{
    prcode(fp,
"\n"
"\n"
"/* The Python module initialisation function. */\n"
"#if defined(SIP_STATIC_MODULE)\n"
"%sPyObject *PyInit_%s(%s)\n"
"#else\n"
"PyMODINIT_FUNC PyInit_%s(%s)\n"
"#endif\n"
"{\n"
        , (gen_c ? "" : "extern \"C\" "), mod->name, (gen_c ? "void" : "")
        , mod->name, (gen_c ? "void" : ""));
}


/*
 * Generate the Python v3 module definition structure.
 */
static void generateModDefinition(moduleDef *mod, const char *methods,
        FILE *fp)
{
    prcode(fp,
"    static PyModuleDef sip_module_def = {\n"
"        PyModuleDef_HEAD_INIT,\n"
"        \"%s\",\n"
        , mod->fullname->text);

    if (mod->docstring == NULL)
        prcode(fp,
"        SIP_NULLPTR,\n"
            );
    else
        prcode(fp,
"        doc_mod_%s,\n"
            , mod->name);

    prcode(fp,
"        -1,\n"
"        %s,\n"
"        SIP_NULLPTR,\n"
"        SIP_NULLPTR,\n"
"        SIP_NULLPTR,\n"
"        SIP_NULLPTR\n"
"    };\n"
        , methods);
}


/*
 * Generate all the sub-class convertors for a module.
 */
static int generateSubClassConvertors(sipSpec *pt, moduleDef *mod, FILE *fp)
{
    int nrSccs = 0;
    classDef *cd;

    for (cd = pt->classes; cd != NULL; cd = cd->next)
    {
        int needs_sipClass;

        if (cd->iff->module != mod)
            continue;

        if (cd->convtosubcode == NULL)
            continue;

        prcode(fp,
"\n"
"\n"
"/* Convert to a sub-class if possible. */\n"
            );

        if (!generating_c)
            prcode(fp,
"extern \"C\" {static const sipTypeDef *sipSubClass_%C(void **);}\n"
                , classFQCName(cd));

        /* Allow the deprecated use of sipClass rather than sipType. */
        needs_sipClass = usedInCode(cd->convtosubcode, "sipClass");

        prcode(fp,
"static const sipTypeDef *sipSubClass_%C(void **sipCppRet)\n"
"{\n"
"    %S *sipCpp = reinterpret_cast<%S *>(*sipCppRet);\n"
            , classFQCName(cd)
            , classFQCName(cd->subbase), classFQCName(cd->subbase));

        if (needs_sipClass)
            prcode(fp,
"    sipWrapperType *sipClass;\n"
"\n"
                );
        else
            prcode(fp,
"    const sipTypeDef *sipType;\n"
"\n"
                );

        generateCppCodeBlock(cd->convtosubcode, fp);

        if (needs_sipClass)
            prcode(fp,
"\n"
"    return (sipClass ? sipClass->wt_td : 0);\n"
"}\n"
                );
        else
            prcode(fp,
"\n"
"    return sipType;\n"
"}\n"
                );

        ++nrSccs;
    }

    return nrSccs;
}


/*
 * Generate the structure representing an encoded type.
 */
static void generateEncodedType(moduleDef *mod, classDef *cd, int last,
        FILE *fp)
{
    moduleDef *cmod = cd->iff->module;

    prcode(fp, "{%u, ", cd->iff->ifacenr);

    if (cmod == mod)
        prcode(fp, "255");
    else
    {
        int mod_nr = 0;
        moduleListDef *mld;

        for (mld = mod->allimports; mld != NULL; mld = mld->next)
        {
            if (mld->module == cmod)
            {
                prcode(fp, "%u", mod_nr);
                break;
            }

            ++mod_nr;
        }
    }

    prcode(fp, ", %u}", last);
}


/*
 * Generate an ordinary function.
 */
static int generateOrdinaryFunction(sipSpec *pt, moduleDef *mod,
        classDef *c_scope, mappedTypeDef *mt_scope, memberDef *md, FILE *fp)
{
    overDef *od;
    int need_intro, has_auto_docstring, self_unused = FALSE;
    ifaceFileDef *scope;
    const char *kw_fw_decl, *kw_decl;

    if (mt_scope != NULL)
    {
        scope = mt_scope->iff;
        od = mt_scope->overs;
    }
    else if (c_scope != NULL)
    {
        scope = (isHiddenNamespace(c_scope) ? NULL : c_scope->iff);
        od = c_scope->overs;
    }
    else
    {
        scope = NULL;
        od = mod->overs;
    }

    prcode(fp,
"\n"
"\n"
        );

    /* Generate the docstrings. */
    if (hasMemberDocstring(pt, od, md))
    {
        if (scope != NULL)
            prcode(fp,
"PyDoc_STRVAR(doc_%L_%s, \"", scope, md->pyname->text);
        else
            prcode(fp,
"PyDoc_STRVAR(doc_%s, \"" , md->pyname->text);

        has_auto_docstring = generateMemberDocstring(pt, od, md, FALSE, fp);

        prcode(fp, "\");\n"
"\n"
            );
    }
    else
    {
        has_auto_docstring = FALSE;
    }

    if (noArgParser(md) || useKeywordArgs(md))
    {
        kw_fw_decl = ", PyObject *";
        kw_decl = ", PyObject *sipKwds";
    }
    else
    {
        kw_fw_decl = "";
        kw_decl = "";
    }

    if (scope != NULL)
    {
        if (!generating_c)
            prcode(fp,
"extern \"C\" {static PyObject *meth_%L_%s(PyObject *, PyObject *%s);}\n"
                , scope, md->pyname->text, kw_fw_decl);

        prcode(fp,
"static PyObject *meth_%L_%s(PyObject *, PyObject *sipArgs%s)\n"
            , scope, md->pyname->text, kw_decl);
    }
    else
    {
        const char *self = (generating_c ? "sipSelf" : "");

        if (!generating_c)
            prcode(fp,
"extern \"C\" {static PyObject *func_%s(PyObject *, PyObject *%s);}\n"
                , md->pyname->text, kw_fw_decl);
        else
            self_unused = TRUE;

        prcode(fp,
"static PyObject *func_%s(PyObject *%s, PyObject *sipArgs%s)\n"
            , md->pyname->text, self, kw_decl);
    }

    prcode(fp,
"{\n"
        );

    need_intro = TRUE;

    while (od != NULL)
    {
        if (od->common == md)
        {
            if (noArgParser(md))
            {
                generateCppCodeBlock(od->methodcode, fp);
                break;
            }

            if (need_intro)
            {
                prcode(fp,
"    PyObject *sipParseErr = SIP_NULLPTR;\n"
                    );

                if (self_unused)
                    prcode(fp,
"\n"
"    (void)sipSelf;\n"
                        );

                need_intro = FALSE;
            }

            if (generateFunctionBody(od, c_scope, mt_scope, c_scope, TRUE, mod, fp) < 0)
                return -1;
        }

        od = od->next;
    }

    if (!need_intro)
    {
        prcode(fp,
"\n"
"    /* Raise an exception if the arguments couldn't be parsed. */\n"
"    sipNoFunction(sipParseErr, %N, ", md->pyname);

        if (has_auto_docstring)
        {
            if (scope != NULL)
                prcode(fp, "doc_%L_%s", scope, md->pyname->text);
            else
                prcode(fp, "doc_%s", md->pyname->text);
        }
        else
        {
            prcode(fp, "SIP_NULLPTR");
        }

        prcode(fp, ");\n"
"\n"
"    return SIP_NULLPTR;\n"
            );
    }

    prcode(fp,
"}\n"
        );

    return 0;
}


/*
 * Generate the table of enum members for a scope.  Return the number of them.
 */
static int generateEnumMemberTable(sipSpec *pt, moduleDef *mod, classDef *cd,
        mappedTypeDef *mtd, FILE *fp)
{
    int i, nr_members;
    enumDef *ed;
    enumMemberDef **etab, **et;

    /* First we count how many. */

    nr_members = 0;

    for (ed = pt->enums; ed != NULL; ed = ed->next)
    {
        enumMemberDef *emd;
        classDef *ps = pyScope(ed->ecd);

        if (ed->module != mod)
            continue;

        if (cd != NULL)
        {
            if (ps != cd || (isProtectedEnum(ed) && !hasShadow(cd)))
                continue;
        }
        else if (mtd != NULL)
        {
            if (ed->emtd != mtd)
                continue;
        }
        else if (ps != NULL || ed->emtd != NULL || ed->fqcname == NULL)
        {
            continue;
        }

        for (emd = ed->members; emd != NULL; emd = emd->next)
            ++nr_members;
    }

    if (nr_members == 0)
        return 0;

    /* Create a table so they can be sorted. */

    etab = sipCalloc(nr_members, sizeof (enumMemberDef *));

    et = etab;

    for (ed = pt->enums; ed != NULL; ed = ed->next)
    {
        enumMemberDef *emd;
        classDef *ps = pyScope(ed->ecd);

        if (ed->module != mod)
            continue;

        if (cd != NULL)
        {
            if (ps != cd)
                continue;
        }
        else if (mtd != NULL)
        {
            if (ed->emtd != mtd)
                continue;
        }
        else if (ps != NULL || ed->emtd != NULL || ed->fqcname == NULL)
        {
            continue;
        }

        for (emd = ed->members; emd != NULL; emd = emd->next)
            *et++ = emd;
    }

    qsort(etab, nr_members, sizeof (enumMemberDef *), compareEnumMembers);

    /* Now generate the table. */

    if (cd == NULL && mtd == NULL)
    {
        prcode(fp,
"\n"
"/* These are the enum members of all global enums. */\n"
"static sipEnumMemberDef enummembers[] = {\n"
        );
    }
    else
    {
        ifaceFileDef *iff = (cd != NULL ? cd->iff : mtd->iff);

        prcode(fp,
"\n"
"static sipEnumMemberDef enummembers_%L[] = {\n"
            , iff);
    }

    for (i = 0; i < nr_members; ++i)
    {
        enumMemberDef *emd;

        emd = etab[i];

        prcode(fp,
"    {%N, ", emd->pyname);

        generateEnumMember(fp, emd, mtd);

        prcode(fp, ", %d},\n", emd->ed->enumnr);
    }

    prcode(fp,
"};\n"
        );

    return nr_members;
}


/*
 * The qsort helper to compare two enumMemberDef structures based on the name
 * of the enum member.
 */
static int compareEnumMembers(const void *m1,const void *m2)
{
    enumMemberDef *emd1 = *(enumMemberDef **)m1;
    enumMemberDef *emd2 = *(enumMemberDef **)m2;

    int cmp = strcmp(emd1->pyname->text, emd2->pyname->text);

    if (cmp == 0)
        if (emd1->ed->enumnr < emd2->ed->enumnr)
            cmp = -1;
        else if (emd1->ed->enumnr > emd2->ed->enumnr)
            cmp = 1;

    return cmp;
}


/*
 * Generate the access functions for the variables.
 */
static void generateAccessFunctions(sipSpec *pt, moduleDef *mod, classDef *cd,
        FILE *fp)
{
    varDef *vd;

    for (vd = pt->vars; vd != NULL; vd = vd->next)
    {
        if (vd->accessfunc == NULL)
            continue;

        if (vd->ecd != cd || vd->module != mod)
            continue;

        prcode(fp,
"\n"
"\n"
"/* Access function. */\n"
            );

        if (!generating_c)
            prcode(fp,
"extern \"C\" {static void *access_%C();}\n"
            , vd->fqcname);

        prcode(fp,
"static void *access_%C()\n"
"{\n"
            , vd->fqcname);

        generateCppCodeBlock(vd->accessfunc, fp);

        prcode(fp,
"}\n"
            );
    }
}


/*
 * Generate the inline code to add a set of Python objects to a module
 * dictionary.  Note that we should add these via a table (like int, etc) but
 * that will require a major API version change so this will do for now.
 */
static void generatePyObjects(sipSpec *pt, moduleDef *mod, FILE *fp)
{
    int noIntro;
    varDef *vd;

    noIntro = TRUE;

    for (vd = pt->vars; vd != NULL; vd = vd->next)
    {
        if (vd->module != mod)
            continue;

        if (vd->type.atype != pyobject_type &&
            vd->type.atype != pytuple_type &&
            vd->type.atype != pylist_type &&
            vd->type.atype != pydict_type &&
            vd->type.atype != pycallable_type &&
            vd->type.atype != pyslice_type &&
            vd->type.atype != pytype_type &&
            vd->type.atype != pybuffer_type &&
            vd->type.atype != pyenum_type)
            continue;

        if (needsHandler(vd))
            continue;

        if (noIntro)
        {
            prcode(fp,
"\n"
"    /* Define the Python objects wrapped as such. */\n"
                );

            noIntro = FALSE;
        }

        prcode(fp,
"    PyDict_SetItemString(sipModuleDict, %N, %S);\n"
                , vd->pyname, vd->fqcname);
    }
}


/*
 * Generate the inline code to add a set of generated type instances to a
 * dictionary.
 */
static void generateTypesInline(sipSpec *pt, moduleDef *mod, FILE *fp)
{
    int noIntro;
    varDef *vd;

    noIntro = TRUE;

    for (vd = pt->vars; vd != NULL; vd = vd->next)
    {
        if (vd->module != mod)
            continue;

        if (vd->type.atype != class_type && vd->type.atype != mapped_type && vd->type.atype != enum_type)
            continue;

        if (needsHandler(vd))
            continue;

        /* Skip classes that don't need inline code. */
        if (generating_c || vd->accessfunc != NULL || vd->type.nrderefs != 0)
            continue;

        if (noIntro)
        {
            prcode(fp,
"\n"
"    /*\n"
"     * Define the class, mapped type and enum instances that have to be\n"
"     * added inline.\n"
"     */\n"
                );

            noIntro = FALSE;
        }

        prcode(fp,
"    sipAddTypeInstance(");

        if (pyScope(vd->ecd) == NULL)
            prcode(fp, "sipModuleDict");
        else
            prcode(fp, "(PyObject *)sipTypeAsPyTypeObject(sipType_%C)", classFQCName(vd->ecd));

        prcode(fp, ",%N,", vd->pyname);

        if (isConstArg(&vd->type))
            prcode(fp, "const_cast<%b *>(&%S)", &vd->type, vd->fqcname);
        else
            prcode(fp, "&%S", vd->fqcname);

        if (vd->type.atype == class_type)
            prcode(fp, ", sipType_%C);\n"
                , classFQCName(vd->type.u.cd));
        else if (vd->type.atype == enum_type)
            prcode(fp, ", sipType_%C);\n"
                , vd->type.u.ed->fqcname);
        else
            prcode(fp, ", sipType_%T);\n"
                , &vd->type);
    }
}


/*
 * Generate the code to add a set of class instances to a dictionary.  Return
 * TRUE if there was at least one.
 */
static int generateClasses(sipSpec *pt, moduleDef *mod, classDef *cd, FILE *fp)
{
    int noIntro;
    varDef *vd;

    noIntro = TRUE;

    for (vd = pt->vars; vd != NULL; vd = vd->next)
    {
        if (pyScope(vd->ecd) != cd || vd->module != mod)
            continue;

        if (vd->type.atype != class_type && (vd->type.atype != enum_type || vd->type.u.ed->fqcname == NULL))
            continue;

        if (needsHandler(vd))
            continue;

        /*
         * Skip ordinary C++ class instances which need to be done with inline
         * code rather than through a static table.  This is because C++ does
         * not guarantee the order in which the table and the instance will be
         * created.  So far this has only been seen to be a problem when
         * statically linking SIP generated modules on Windows.
         */
        if (!generating_c && vd->accessfunc == NULL && vd->type.nrderefs == 0)
            continue;

        if (noIntro)
        {
            if (cd != NULL)
                prcode(fp,
"\n"
"\n"
"/* Define the class and enum instances to be added to this type dictionary. */\n"
"static sipTypeInstanceDef typeInstances_%C[] = {\n"
                    , classFQCName(cd));
            else
                prcode(fp,
"\n"
"\n"
"/* Define the class and enum instances to be added to this module dictionary. */\n"
"static sipTypeInstanceDef typeInstances[] = {\n"
                    );

            noIntro = FALSE;
        }

        prcode(fp,
"    {%N, ", vd->pyname);

        if (vd->type.atype == class_type)
        {
            scopedNameDef *vcname = classFQCName(vd->type.u.cd);

            if (vd->accessfunc != NULL)
            {
                prcode(fp, "(void *)access_%C, &sipType_%C, SIP_ACCFUNC|SIP_NOT_IN_MAP", vd->fqcname, vcname);
            }
            else if (vd->type.nrderefs != 0)
            {
                /* This may be a bit heavy handed. */
                if (isConstArg(&vd->type))
                    prcode(fp, "(void *)");

                prcode(fp, "&%S, &sipType_%C, SIP_INDIRECT", vd->fqcname, vcname);
            }
            else if (isConstArg(&vd->type))
            {
                prcode(fp, "const_cast<%b *>(&%S), &sipType_%C, 0", &vd->type, vd->fqcname, vcname);
            }
            else
            {
                prcode(fp, "&%S, &sipType_%C, 0", vd->fqcname, vcname);
            }
        }
        else
        {
            prcode(fp, "&%S, &sipType_%C, 0", vd->fqcname, vd->type.u.ed->fqcname);
        }

        prcode(fp, "},\n"
            );
    }

    if (!noIntro)
        prcode(fp,
"    {0, 0, 0, 0}\n"
"};\n"
            );

    return !noIntro;
}


/*
 * Generate the code to add a set of void pointers to a dictionary.  Return
 * TRUE if there was at least one.
 */
static int generateVoidPointers(sipSpec *pt, moduleDef *mod, classDef *cd,
        FILE *fp)
{
    int noIntro;
    varDef *vd;

    noIntro = TRUE;

    for (vd = pt->vars; vd != NULL; vd = vd->next)
    {
        if (pyScope(vd->ecd) != cd || vd->module != mod)
            continue;

        if (vd->type.atype != void_type && vd->type.atype != struct_type && vd->type.atype != union_type)
            continue;

        if (needsHandler(vd))
            continue;

        if (noIntro)
        {
            if (cd != NULL)
                prcode(fp,
"\n"
"\n"
"/* Define the void pointers to be added to this type dictionary. */\n"
"static sipVoidPtrInstanceDef voidPtrInstances_%C[] = {\n"
                    , classFQCName(cd));
            else
                prcode(fp,
"\n"
"\n"
"/* Define the void pointers to be added to this module dictionary. */\n"
"static sipVoidPtrInstanceDef voidPtrInstances[] = {\n"
                    );

            noIntro = FALSE;
        }

        if (isConstArg(&vd->type))
            prcode(fp,
"    {%N, const_cast<%b *>(%S)},\n"
                , vd->pyname, &vd->type, vd->fqcname);
        else
            prcode(fp,
"    {%N, %S},\n"
                , vd->pyname, vd->fqcname);
    }

    if (!noIntro)
        prcode(fp,
"    {0, 0}\n"
"};\n"
            );

    return !noIntro;
}


/*
 * Generate the code to add a set of characters to a dictionary.  Return TRUE
 * if there was at least one.
 */
static int generateChars(sipSpec *pt, moduleDef *mod, classDef *cd, FILE *fp)
{
    int noIntro;
    varDef *vd;

    noIntro = TRUE;

    for (vd = pt->vars; vd != NULL; vd = vd->next)
    {
        argType vtype = vd->type.atype;

        if (pyScope(vd->ecd) != cd || vd->module != mod)
            continue;

        if (!((vtype == ascii_string_type || vtype == latin1_string_type || vtype == utf8_string_type || vtype == sstring_type || vtype == ustring_type || vtype == string_type) && vd->type.nrderefs == 0))
            continue;

        if (needsHandler(vd))
            continue;

        if (noIntro)
        {
            if (cd != NULL)
                prcode(fp,
"\n"
"\n"
"/* Define the chars to be added to this type dictionary. */\n"
"static sipCharInstanceDef charInstances_%C[] = {\n"
                    , classFQCName(cd));
            else
                prcode(fp,
"\n"
"\n"
"/* Define the chars to be added to this module dictionary. */\n"
"static sipCharInstanceDef charInstances[] = {\n"
                    );

            noIntro = FALSE;
        }

        prcode(fp,
"    {%N, %S, '%c'},\n"
            , vd->pyname, (cd != NULL ? vd->fqcname : vd->fqcname->next), getEncoding(&vd->type));
    }

    if (!noIntro)
        prcode(fp,
"    {0, 0, 0}\n"
"};\n"
            );

    return !noIntro;
}


/*
 * Generate the code to add a set of strings to a dictionary.  Return TRUE if
 * there is at least one.
 */
static int generateStrings(sipSpec *pt, moduleDef *mod, classDef *cd, FILE *fp)
{
    int noIntro;
    varDef *vd;

    noIntro = TRUE;

    for (vd = pt->vars; vd != NULL; vd = vd->next)
    {
        argType vtype = vd->type.atype;
        const char *cast;
        char encoding;

        if (pyScope(vd->ecd) != cd || vd->module != mod)
            continue;

        if (!(((vtype == ascii_string_type || vtype == latin1_string_type || vtype == utf8_string_type || vtype == sstring_type || vtype == ustring_type || vtype == string_type) && vd->type.nrderefs != 0) || vtype == wstring_type))
            continue;

        if (needsHandler(vd))
            continue;

        if (noIntro)
        {
            if (cd != NULL)
                prcode(fp,
"\n"
"\n"
"/* Define the strings to be added to this type dictionary. */\n"
"static sipStringInstanceDef stringInstances_%C[] = {\n"
                    , classFQCName(cd));
            else
                prcode(fp,
"\n"
"\n"
"/* Define the strings to be added to this module dictionary. */\n"
"static sipStringInstanceDef stringInstances[] = {\n"
                    );

            noIntro = FALSE;
        }

        /* This is the hack for handling wchar_t and wchar_t*. */
        encoding = getEncoding(&vd->type);

        if (encoding == 'w')
            cast = "(const char *)&";
        else if (encoding == 'W')
            cast = "(const char *)";
        else
            cast = "";

        prcode(fp,
"    {%N, %s%S, '%c'},\n"
            , vd->pyname, cast, (cd != NULL ? vd->fqcname : vd->fqcname->next), encoding);
    }

    if (!noIntro)
        prcode(fp,
"    {0, 0, 0}\n"
"};\n"
            );

    return !noIntro;
}


/*
 * Generate the code to add a set of ints.  Return TRUE if there was at least
 * one.
 */
static int generateInts(sipSpec *pt, moduleDef *mod, ifaceFileDef *iff,
        FILE *fp)
{
    int noIntro;
    enumDef *ed;
    varDef *vd;

    noIntro = TRUE;

    if (abiVersion >= ABI_13_0)
    {
        int i;
        argDef *ad;

        /*
         * Named enum members are handled as int variables but must be placed
         * at the start of the table.  Not we use the sorted table of needed
         * types rather than the unsorted table of all enums.
         */
        for (ad = mod->needed_types, i = 0; i < mod->nr_needed_types; ++i, ++ad)
        {
            enumMemberDef *em;

            if (ad->atype != enum_type)
                continue;

            ed = ad->u.ed;

            if (pyEnumScopeIface(ed) != iff || ed->module != mod)
                continue;

            for (em = ed->members; em != NULL; em = em->next)
            {
                if (noIntro)
                {
                    ints_intro(iff, fp);
                    noIntro = FALSE;
                }

                prcode(fp,
"    {%N, ", em->pyname);

                generateEnumMember(fp, em, ed->emtd);

                prcode(fp, "},\n"
                    );
            }
        }
    }

    /* Handle int variables. */
    for (vd = pt->vars; vd != NULL; vd = vd->next)
    {
        argType vtype = vd->type.atype;

        if (pyScopeIface(vd->ecd) != iff || vd->module != mod)
            continue;

        if (!(vtype == enum_type || vtype == byte_type ||
              vtype == sbyte_type || vtype == ubyte_type ||
              vtype == ushort_type || vtype == short_type ||
              vtype == cint_type || vtype == int_type ||
              vtype == bool_type || vtype == cbool_type))
            continue;

        if (needsHandler(vd))
            continue;

        /* Named enums are handled elsewhere. */
        if (vtype == enum_type && vd->type.u.ed->fqcname != NULL)
            continue;

        if (noIntro)
        {
            ints_intro(iff, fp);
            noIntro = FALSE;
        }

        prcode(fp,
"    {%N, %S},\n"
            , vd->pyname, (iff != NULL ? vd->fqcname : vd->fqcname->next));
    }

    /* Anonymous enum members are handled as int variables. */
    if (abiVersion >= ABI_13_0 || iff == NULL)
    {
        for (ed = pt->enums; ed != NULL; ed = ed->next)
        {
            enumMemberDef *em;

            if (pyEnumScopeIface(ed) != iff || ed->module != mod)
                continue;

            if (ed->fqcname != NULL)
                continue;

            for (em = ed->members; em != NULL; em = em->next)
            {
                if (noIntro)
                {
                    ints_intro(iff, fp);
                    noIntro = FALSE;
                }

                prcode(fp,
"    {%N, ", em->pyname);

                generateEnumMember(fp, em, ed->emtd);

                prcode(fp, "},\n"
                    );
            }
        }
    }

    if (!noIntro)
        prcode(fp,
"    {0, 0}\n"
"};\n"
            );

    return !noIntro;
}


/*
 * Generate the intro for a table of int instances.
 */
static void ints_intro(ifaceFileDef *iff, FILE *fp)
{
    if (iff != NULL)
        prcode(fp,
"\n"
"\n"
"/* Define the enum members and ints to be added to this type. */\n"
"static sipIntInstanceDef intInstances_%L[] = {\n"
            , iff);
    else
        prcode(fp,
"\n"
"\n"
"/* Define the enum members and ints to be added to this module. */\n"
"static sipIntInstanceDef intInstances[] = {\n"
            );
}


/*
 * Generate the code to add a set of longs to a dictionary.  Return TRUE if
 * there was at least one.
 */
static int generateLongs(sipSpec *pt, moduleDef *mod, classDef *cd, FILE *fp)
{
    return generateVariableType(pt, mod, cd, long_type, "long", "Long", "long", fp);
}


/*
 * Generate the code to add a set of unsigned longs to a dictionary.  Return
 * TRUE if there was at least one.
 */
static int generateUnsignedLongs(sipSpec *pt, moduleDef *mod, classDef *cd,
        FILE *fp)
{
    return generateVariableType(pt, mod, cd, ulong_type, "unsigned long", "UnsignedLong", "unsignedLong", fp);
}


/*
 * Generate the code to add a set of long longs to a dictionary.  Return TRUE
 * if there was at least one.
 */
static int generateLongLongs(sipSpec *pt, moduleDef *mod, classDef *cd,
        FILE *fp)
{
    return generateVariableType(pt, mod, cd, longlong_type, "long long", "LongLong", "longLong", fp);
}


/*
 * Generate the code to add a set of unsigned long longs to a dictionary.
 * Return TRUE if there was at least one.
 */
static int generateUnsignedLongLongs(sipSpec *pt, moduleDef *mod, classDef *cd,
        FILE *fp)
{
    return generateVariableType(pt, mod, cd, ulonglong_type, "unsigned long long", "UnsignedLongLong", "unsignedLongLong", fp);
}


/*
 * Generate the code to add a set of a particular type to a dictionary.  Return
 * TRUE if there was at least one.
 */
static int generateVariableType(sipSpec *pt, moduleDef *mod, classDef *cd,
        argType atype, const char *eng, const char *s1, const char *s2,
        FILE *fp)
{
    int noIntro;
    varDef *vd;

    noIntro = TRUE;

    for (vd = pt->vars; vd != NULL; vd = vd->next)
    {
        argType vtype = vd->type.atype;

        /*
         * We treat unsigned and size_t as unsigned long as we don't (currently
         * anyway) generate a separate table for them.
         */
        if ((vtype == uint_type || vtype == size_type) && atype == ulong_type)
            vtype = ulong_type;

        if (pyScope(vd->ecd) != cd || vd->module != mod)
            continue;

        if (vtype != atype)
            continue;

        if (needsHandler(vd))
            continue;

        if (noIntro)
        {
            if (cd != NULL)
                prcode(fp,
"\n"
"\n"
"/* Define the %ss to be added to this type dictionary. */\n"
"static sip%sInstanceDef %sInstances_%C[] = {\n"
                    , eng
                    , s1, s2, classFQCName(cd));
            else
                prcode(fp,
"\n"
"\n"
"/* Define the %ss to be added to this module dictionary. */\n"
"static sip%sInstanceDef %sInstances[] = {\n"
                    , eng
                    , s1, s2);

            noIntro = FALSE;
        }

        prcode(fp,
"    {%N, %S},\n"
            , vd->pyname, (cd != NULL ? vd->fqcname : vd->fqcname->next));
    }

    if (!noIntro)
        prcode(fp,
"    {0, 0}\n"
"};\n"
            );

    return !noIntro;
}


/*
 * Generate the code to add a set of doubles to a dictionary.  Return TRUE if
 * there was at least one.
 */
static int generateDoubles(sipSpec *pt, moduleDef *mod, classDef *cd, FILE *fp)
{
    int noIntro;
    varDef *vd;

    noIntro = TRUE;

    for (vd = pt->vars; vd != NULL; vd = vd->next)
    {
        argType vtype = vd->type.atype;

        if (pyScope(vd->ecd) != cd || vd->module != mod)
            continue;

        if (!(vtype == float_type || vtype == cfloat_type || vtype == double_type || vtype == cdouble_type))
            continue;

        if (needsHandler(vd))
            continue;

        if (noIntro)
        {
            if (cd != NULL)
                prcode(fp,
"\n"
"\n"
"/* Define the doubles to be added to this type dictionary. */\n"
"static sipDoubleInstanceDef doubleInstances_%C[] = {\n"
                    , classFQCName(cd));
            else
                prcode(fp,
"\n"
"\n"
"/* Define the doubles to be added to this module dictionary. */\n"
"static sipDoubleInstanceDef doubleInstances[] = {\n"
                    );

            noIntro = FALSE;
        }

        prcode(fp,
"    {%N, %S},\n"
            , vd->pyname, (cd != NULL ? vd->fqcname : vd->fqcname->next));
    }

    if (!noIntro)
        prcode(fp,
"    {0, 0}\n"
"};\n"
            );

    return !noIntro;
}


/*
 * See if an interface file has any content.
 */
static int emptyIfaceFile(sipSpec *pt, ifaceFileDef *iff)
{
    classDef *cd;
    mappedTypeDef *mtd;

    for (cd = pt->classes; cd != NULL; cd = cd->next)
        if (!isHiddenNamespace(cd) && !isProtectedClass(cd) && !isExternal(cd) && cd->iff == iff)
            return FALSE;

    for (mtd = pt->mappedtypes; mtd != NULL; mtd = mtd->next)
        if (mtd->iff == iff)
            return FALSE;

    return TRUE;
}


/*
 * Generate the C/C++ code for an interface.
 */
static int generateIfaceCpp(sipSpec *pt, stringList **generated, int py_debug,
        ifaceFileDef *iff, int need_postinc, const char *codeDir,
        const char *srcSuffix, FILE *master)
{
    char *cppfile;
    const char *cmname = iff->module->name;
    classDef *cd;
    mappedTypeDef *mtd;
    FILE *fp;

    /*
     * Check that there will be something in the file so that we don't get
     * warning messages from ranlib.
     */
    if (emptyIfaceFile(pt, iff))
        return 0;

    if (master == NULL)
    {
        cppfile = createIfaceFileName(codeDir,iff,srcSuffix);
        if ((fp = createCompilationUnit(iff->module, generated, cppfile, "Interface wrapper code.")) == NULL)
            return -1;

        prcode(fp,
"\n"
"#include \"sipAPI%s.h\"\n"
            , cmname);

        need_postinc = TRUE;
    }
    else
    {
        fp = master;

        /* Suppress a compiler warning. */
        cppfile = NULL;
    }

    prcode(fp,
"\n"
            );

    generateCppCodeBlock(iff->hdrcode, fp);
    generateUsedIncludes(iff->used, fp);

    if (need_postinc)
        generateCppCodeBlock(iff->module->unitpostinccode, fp);

    for (cd = pt->classes; cd != NULL; cd = cd->next)
    {
        /*
         * Protected classes must be generated in the interface file of the
         * enclosing scope.
         */
        if (isProtectedClass(cd))
            continue;

        if (isExternal(cd))
            continue;

        if (cd->iff == iff)
        {
            classDef *pcd;

            if (generateClassCpp(cd, pt, py_debug, fp) < 0)
                return -1;

            /* Generate any enclosed protected classes. */
            for (pcd = pt->classes; pcd != NULL; pcd = pcd->next)
                if (isProtectedClass(pcd) && pcd->ecd == cd)
                    if (generateClassCpp(pcd, pt, py_debug, fp) < 0)
                        return -1;
        }
    }

    for (mtd = pt->mappedtypes; mtd != NULL; mtd = mtd->next)
        if (mtd->iff == iff)
            if (generateMappedTypeCpp(mtd, pt, fp) < 0)
                return -1;

    if (master == NULL)
    {
        if (closeFile(fp) < 0)
            return -1;

        free(cppfile);
    }

    return 0;
}


/*
 * Return a filename for an interface C++ or header file on the heap.
 */
static char *createIfaceFileName(const char *codeDir, ifaceFileDef *iff,
        const char *suffix)
{
    char *fn;
    scopedNameDef *snd;

    fn = concat(codeDir,"/sip",iff->module->name,NULL);

    for (snd = iff->fqcname; snd != NULL; snd = snd->next)
        append(&fn,snd->name);

    if (iff->file_extension != NULL)
        suffix = iff->file_extension;

    append(&fn,suffix);

    return fn;
}


/*
 * Generate the C++ code for a mapped type version.
 */
static int generateMappedTypeCpp(mappedTypeDef *mtd, sipSpec *pt, FILE *fp)
{
    int nr_methods, nr_enums, has_ints, needs_namespace, plugin;
    int need_state, need_user_state;
    memberDef *md;

    generateCppCodeBlock(mtd->typecode, fp);

    if (!noRelease(mtd))
    {
        /*
         * Generate the assignment helper.  Note that the source pointer is not
         * const.  This is to allow the source instance to be modified as a
         * consequence of the assignment, eg. if it is implementing some sort
         * of reference counting scheme.
         */
        if (!noAssignOp(mtd))
        {
            prcode(fp,
"\n"
"\n"
                );

            if (!generating_c)
                prcode(fp,
"extern \"C\" {static void assign_%L(void *, Py_ssize_t, void *);}\n"
                    , mtd->iff);

            prcode(fp,
"static void assign_%L(void *sipDst, Py_ssize_t sipDstIdx, void *sipSrc)\n"
"{\n"
                , mtd->iff);

            if (generating_c)
                prcode(fp,
"    ((%b *)sipDst)[sipDstIdx] = *((%b *)sipSrc);\n"
                    , &mtd->type, &mtd->type);
            else
                prcode(fp,
"    reinterpret_cast<%b *>(sipDst)[sipDstIdx] = *reinterpret_cast<%b *>(sipSrc);\n"
                    , &mtd->type, &mtd->type);

            prcode(fp,
"}\n"
                );
        }

        /* Generate the array allocation helper. */
        if (!noDefaultCtor(mtd))
        {
            prcode(fp,
"\n"
"\n"
                );

            if (!generating_c)
                prcode(fp,
"extern \"C\" {static void *array_%L(Py_ssize_t);}\n"
                    , mtd->iff);

            prcode(fp,
"static void *array_%L(Py_ssize_t sipNrElem)\n"
"{\n"
                , mtd->iff);

            if (generating_c)
                prcode(fp,
"    return sipMalloc(sizeof (%b) * sipNrElem);\n"
                    , &mtd->type);
            else
                prcode(fp,
"    return new %b[sipNrElem];\n"
                    , &mtd->type);

            prcode(fp,
"}\n"
                );
        }

        /* Generate the copy helper. */
        if (!noCopyCtor(mtd))
        {
            prcode(fp,
"\n"
"\n"
                );

            if (!generating_c)
                prcode(fp,
"extern \"C\" {static void *copy_%L(const void *, Py_ssize_t);}\n"
                    , mtd->iff);

            prcode(fp,
"static void *copy_%L(const void *sipSrc, Py_ssize_t sipSrcIdx)\n"
"{\n"
                , mtd->iff);

            if (generating_c)
                prcode(fp,
"    %b *sipPtr = sipMalloc(sizeof (%b));\n"
"    *sipPtr = ((const %b *)sipSrc)[sipSrcIdx];\n"
"\n"
"    return sipPtr;\n"
                    , &mtd->type, &mtd->type
                    , &mtd->type);
            else
                prcode(fp,
"    return new %b(reinterpret_cast<const %b *>(sipSrc)[sipSrcIdx]);\n"
                    , &mtd->type, &mtd->type);

            prcode(fp,
"}\n"
                );
        }

        prcode(fp,
"\n"
"\n"
"/* Call the mapped type's destructor. */\n"
            );

        need_state = usedInCode(mtd->releasecode, "sipState");
        need_user_state = usedInCode(mtd->releasecode, "sipUserState");

        if (!generating_c)
            prcode(fp,
"extern \"C\" {static void release_%L(void *, int%s);}\n"
                , mtd->iff, (abiVersion >= ABI_13_0 ? ", void *" : ""));

        prcode(fp,
"static void release_%L(void *sipCppV, int%s", mtd->iff, ((generating_c || need_state) ? " sipState" : ""));

        if (abiVersion >= ABI_13_0)
            prcode(fp, ", void *%s", (need_user_state ? "sipUserState" : ""));

        prcode(fp, ")\n"
"{\n"
"    ");

        generateMappedTypeFromVoid(mtd, "sipCpp", "sipCppV", fp);

        prcode(fp, ";\n"
            );

        if (release_gil)
            prcode(fp,
"    Py_BEGIN_ALLOW_THREADS\n"
                );

        if (mtd->releasecode != NULL)
            generateCppCodeBlock(mtd->releasecode, fp);
        else if (generating_c)
            prcode(fp,
"    sipFree(sipCpp);\n"
                );
        else
            prcode(fp,
"    delete sipCpp;\n"
                );

        if (release_gil)
            prcode(fp,
"    Py_END_ALLOW_THREADS\n"
                );

        prcode(fp,
"}\n"
"\n"
            );
    }

    generateConvertToDefinitions(mtd,NULL,fp);

    /* Generate the from type convertor. */
    if (mtd->convfromcode != NULL)
    {
        int need_xfer = (generating_c || usedInCode(mtd->convfromcode, "sipTransferObj"));

        prcode(fp,
"\n"
"\n"
            );

        if (!generating_c)
            prcode(fp,
"extern \"C\" {static PyObject *convertFrom_%L(void *, PyObject *);}\n"
                , mtd->iff);

        prcode(fp,
"static PyObject *convertFrom_%L(void *sipCppV, PyObject *%s)\n"
"{\n"
"    ", mtd->iff, (need_xfer ? "sipTransferObj" : ""));

        generateMappedTypeFromVoid(mtd, "sipCpp", "sipCppV", fp);

        prcode(fp, ";\n"
"\n"
            );

        generateCppCodeBlock(mtd->convfromcode,fp);

        prcode(fp,
"}\n"
            );
    }

    /* Generate the static methods. */
    for (md = mtd->members; md != NULL; md = md->next)
        if (generateOrdinaryFunction(pt, mtd->iff->module, NULL, mtd, md, fp) < 0)
            return -1;

    nr_methods = generateMappedTypeMethodTable(pt, mtd, fp);

    if (abiVersion >= ABI_13_0)
    {
        nr_enums = -1;
        has_ints = generateInts(pt, mtd->iff->module, mtd->iff, fp);
        needs_namespace = FALSE;
    }
    else
    {
        nr_enums = generateEnumMemberTable(pt, mtd->iff->module, NULL, mtd,
                fp);
        has_ints = FALSE;
        needs_namespace = (nr_enums > 0);
    }

    if (nr_methods > 0)
        needs_namespace = TRUE;

    if (pluginPyQt6(pt))
        plugin = generatePyQt6MappedTypePlugin(pt, mtd, fp);
    else
        plugin = FALSE;

    prcode(fp,
"\n"
"\n"
"sipMappedTypeDef ");

    generateTypeDefName(mtd->iff, fp);

    prcode(fp, " = {\n"
"    {\n"
        );

    if (abiVersion < ABI_13_0)
        prcode(fp,
"        -1,\n"
"        SIP_NULLPTR,\n"
            );

    prcode(fp,
"        SIP_NULLPTR,\n"
"        %s%sSIP_TYPE_MAPPED,\n"
"        %n,\n"
"        SIP_NULLPTR,\n"
        , (handlesNone(mtd) ? "SIP_TYPE_ALLOW_NONE|" : "")
        , (needsUserState(mtd) ? "SIP_TYPE_USER_STATE|" : "")
        , mtd->cname);

    if (plugin)
        prcode(fp,
"        &plugin_%L,\n"
            , mtd->iff);
    else
        prcode(fp,
"        SIP_NULLPTR,\n"
            );

    prcode(fp,
"    },\n"
"    {\n"
            );

    if (needs_namespace)
        prcode(fp,
"        %n,\n"
            , mtd->pyname);
    else
        prcode(fp,
"        -1,\n"
            );

    prcode(fp,
"        {0, 0, 1},\n"
        );

    if (nr_methods == 0)
        prcode(fp,
"        0, SIP_NULLPTR,\n"
            );
    else
        prcode(fp,
"        %d, methods_%L,\n"
            , nr_methods, mtd->iff);

    if (nr_enums == 0)
        prcode(fp,
"        0, SIP_NULLPTR,\n"
            );
    else if (nr_enums > 0)
        prcode(fp,
"        %d, enummembers_%L,\n"
            , nr_enums, mtd->iff);

    prcode(fp,
"        0, SIP_NULLPTR,\n"
"        {SIP_NULLPTR, SIP_NULLPTR, SIP_NULLPTR, SIP_NULLPTR, ");

    if (has_ints)
        prcode(fp, "intInstances_%L", mtd->iff);
    else
        prcode(fp, "SIP_NULLPTR");

    prcode(fp, ", SIP_NULLPTR, SIP_NULLPTR, SIP_NULLPTR, SIP_NULLPTR, SIP_NULLPTR}\n"
"    },\n"
        );

    if (!noAssignOp(mtd))
        prcode(fp,
"    assign_%L,\n"
        , mtd->iff);
    else
        prcode(fp,
"    SIP_NULLPTR,\n"
            );

    if (!noDefaultCtor(mtd))
        prcode(fp,
"    array_%L,\n"
        , mtd->iff);
    else
        prcode(fp,
"    SIP_NULLPTR,\n"
            );

    if (!noCopyCtor(mtd))
        prcode(fp,
"    copy_%L,\n"
        , mtd->iff);
    else
        prcode(fp,
"    SIP_NULLPTR,\n"
            );

    if (!noRelease(mtd))
        prcode(fp,
"    release_%L,\n"
            , mtd->iff);
    else
        prcode(fp,
"    SIP_NULLPTR,\n"
            );

    if (mtd->convtocode != NULL)
    {
        prcode(fp,
"    convertTo_%L,\n"
            , mtd->iff);
    }
    else
    {
        prcode(fp,
"    SIP_NULLPTR,\n"
            );
    }

    if (mtd->convfromcode != NULL)
    {
        prcode(fp,
"    convertFrom_%L\n"
            , mtd->iff);
    }
    else
    {
        prcode(fp,
"    SIP_NULLPTR\n"
            );
    }

    prcode(fp,
"};\n"
        );

    return 0;
}


/*
 * Generate the name of the type structure for a class or mapped type.
 */
static void generateTypeDefName(ifaceFileDef *iff, FILE *fp)
{
    prcode(fp, "sipTypeDef_%s_%L", iff->module->name, iff);
}


/*
 * Generate the C++ code for a class.
 */
static int generateClassCpp(classDef *cd, sipSpec *pt, int py_debug, FILE *fp)
{
    moduleDef *mod = cd->iff->module;

    /* Generate any local class code. */

    generateCppCodeBlock(cd->cppcode, fp);

    if (generateClassFunctions(pt, mod, cd, py_debug, fp) < 0)
        return -1;

    generateAccessFunctions(pt, mod, cd, fp);

    if (cd->iff->type != namespace_iface)
    {
        generateConvertToDefinitions(NULL,cd,fp);

        /* Generate the optional from type convertor. */
        if (cd->convfromcode != NULL)
        {
            int need_xfer;

            need_xfer = (generating_c || usedInCode(cd->convfromcode, "sipTransferObj"));

            prcode(fp,
"\n"
"\n"
                );

            if (!generating_c)
                prcode(fp,
"extern \"C\" {static PyObject *convertFrom_%L(void *, PyObject *);}\n"
                    , cd->iff);

            prcode(fp,
"static PyObject *convertFrom_%L(void *sipCppV, PyObject *%s)\n"
"{\n"
"    ", cd->iff, (need_xfer ? "sipTransferObj" : ""));

            generateClassFromVoid(cd, "sipCpp", "sipCppV", fp);

            prcode(fp, ";\n"
"\n"
                );

            generateCppCodeBlock(cd->convfromcode, fp);

            prcode(fp,
"}\n"
                );
        }
    }

    /* The type definition structure. */
    if (generateTypeDefinition(pt, cd, py_debug, fp) < 0)
        return -1;

    return 0;
}


/*
 * Return a sorted array of relevant functions for a namespace.
 */

static sortedMethTab *createFunctionTable(memberDef *members, int *nrp)
{
    int nr;
    sortedMethTab *mtab, *mt;
    memberDef *md;

    /* First we need to count the number of applicable functions. */
    nr = 0;

    for (md = members; md != NULL; md = md->next)
        ++nr;

    if ((*nrp = nr) == 0)
        return NULL;

    /* Create the table of methods. */
    mtab = sipCalloc(nr, sizeof (sortedMethTab));

    /* Initialise the table. */
    mt = mtab;

    for (md = members; md != NULL; md = md->next)
    {
        mt->md = md;
        ++mt;
    }

    /* Finally, sort the table. */
    qsort(mtab,nr,sizeof (sortedMethTab),compareMethTab);

    return mtab;
}


/*
 * Return a sorted array of relevant methods (either lazy or non-lazy) for a
 * class.
 */
static sortedMethTab *createMethodTable(classDef *cd, int *nrp)
{
    int nr;
    visibleList *vl;
    sortedMethTab *mtab, *mt;

    /*
     * First we need to count the number of applicable methods.  Only provide
     * an entry point if there is at least one overload that is defined in this
     * class and is a non-abstract function or slot.  We allow private (even
     * though we don't actually generate code) because we need to intercept the
     * name before it reaches a more public version further up the class
     * hierarchy.  We add the ctor and any variable handlers as special
     * entries.
     */
    nr = 0;

    for (vl = cd->visible; vl != NULL; vl = vl->next)
    {
        overDef *od;

        if (vl->m->slot != no_slot)
            continue;

        for (od = vl->cd->overs; od != NULL; od = od->next)
        {
            /*
             * Skip protected methods if we don't have the means to handle
             * them.
             */
            if (isProtected(od) && !hasShadow(cd))
                continue;

            if (skipOverload(od,vl->m,cd,vl->cd,TRUE))
                continue;

            ++nr;

            break;
        }
    }

    if ((*nrp = nr) == 0)
        return NULL;

    /* Create the table of methods. */

    mtab = sipCalloc(nr, sizeof (sortedMethTab));

    /* Initialise the table. */

    mt = mtab;

    for (vl = cd->visible; vl != NULL; vl = vl->next)
    {
        int need_method;
        overDef *od;

        if (vl->m->slot != no_slot)
            continue;

        need_method = FALSE;

        for (od = vl->cd->overs; od != NULL; od = od->next)
        {
            /*
             * Skip protected methods if we don't have the means to handle
             * them.
             */
            if (isProtected(od) && !hasShadow(cd))
                continue;

            if (!skipOverload(od,vl->m,cd,vl->cd,TRUE))
                need_method = TRUE;
        }

        if (need_method)
        {
            mt->md = vl->m;
            ++mt;
        }
    }

    /* Finally sort the table. */

    qsort(mtab,nr,sizeof (sortedMethTab),compareMethTab);

    return mtab;
}


/*
 * The qsort helper to compare two sortedMethTab structures based on the Python
 * name of the method.
 */

static int compareMethTab(const void *m1,const void *m2)
{
    return strcmp(((sortedMethTab *)m1)->md->pyname->text,
              ((sortedMethTab *)m2)->md->pyname->text);
}


/*
 * Generate the sorted table of static methods for a mapped type and return
 * the number of entries.
 */
static int generateMappedTypeMethodTable(sipSpec *pt, mappedTypeDef *mtd,
        FILE *fp)
{
    int nr;
    sortedMethTab *mtab;

    mtab = createFunctionTable(mtd->members, &nr);

    if (mtab != NULL)
    {
        prMethodTable(pt, mtab, nr, mtd->iff, mtd->overs, fp);
        free(mtab);
    }

    return nr;
}


/*
 * Generate the sorted table of methods for a class and return the number of
 * entries.
 */
static int generateClassMethodTable(sipSpec *pt, classDef *cd, FILE *fp)
{
    int nr;
    sortedMethTab *mtab;

    mtab = (cd->iff->type == namespace_iface) ?
            createFunctionTable(cd->members, &nr) :
            createMethodTable(cd, &nr);

    if (mtab != NULL)
    {
        prMethodTable(pt, mtab, nr, cd->iff, cd->overs, fp);
        free(mtab);
    }

    return nr;
}


/*
 * Generate a method table for a class or mapped type.
 */
static void prMethodTable(sipSpec *pt, sortedMethTab *mtable, int nr,
        ifaceFileDef *iff, overDef *overs, FILE *fp)
{
    int i;

    prcode(fp,
"\n"
"\n"
"static PyMethodDef methods_%L[] = {\n"
        , iff);

    for (i = 0; i < nr; ++i)
    {
        memberDef *md = mtable[i].md;
        const char *cast, *cast_suffix, *flags;

        if (noArgParser(md) || useKeywordArgs(md))
        {
            cast = "SIP_MLMETH_CAST(";
            cast_suffix = ")";
            flags = "|METH_KEYWORDS";
        }
        else
        {
            cast = "";
            cast_suffix = "";
            flags = "";
        }

        /* Save the index in the table. */
        md->membernr = i;

        prcode(fp,
"    {%N, %smeth_%L_%s%s, METH_VARARGS%s, ", md->pyname, cast, iff, md->pyname->text, cast_suffix, flags);

        if (hasMemberDocstring(pt, overs, md))
            prcode(fp, "doc_%L_%s", iff, md->pyname->text);
        else
            prcode(fp, "SIP_NULLPTR");

        prcode(fp, "}%s\n"
            , ((i + 1) < nr) ? "," : "");
    }

    prcode(fp,
"};\n"
        );
}


/*
 * Generate the "to type" convertor definitions.
 */

static void generateConvertToDefinitions(mappedTypeDef *mtd,classDef *cd,
                     FILE *fp)
{
    codeBlockList *convtocode;
    ifaceFileDef *iff;
    argDef type;

    memset(&type, 0, sizeof (argDef));

    if (cd != NULL)
    {
        convtocode = cd->convtocode;
        iff = cd->iff;

        type.atype = class_type;
        type.u.cd = cd;
    }
    else
    {
        if ((convtocode = mtd->convtocode) == NULL)
            return;

        iff = mtd->iff;

        type.atype = mapped_type;
        type.u.mtd = mtd;
    }

    /* Generate the type convertors. */

    if (convtocode != NULL)
    {
        int need_py, need_ptr, need_iserr, need_xfer, need_us_arg, need_us_val;

        /*
         * Sometimes type convertors are just stubs that set the error flag, so
         * check if we actually need everything so that we can avoid compiler
         * warnings.
         */
        need_py = (generating_c || usedInCode(convtocode, "sipPy"));
        need_ptr = (generating_c || usedInCode(convtocode, "sipCppPtr"));
        need_iserr = (generating_c || usedInCode(convtocode, "sipIsErr"));
        need_xfer = (generating_c || usedInCode(convtocode, "sipTransferObj"));

        if (abiVersion >= ABI_13_0)
        {
            need_us_arg = TRUE;
            need_us_val = (generating_c || typeNeedsUserState(&type));
        }
        else
        {
            need_us_arg = FALSE;
            need_us_val = FALSE;
        }

        prcode(fp,
"\n"
"\n"
            );

        if (!generating_c)
            prcode(fp,
"extern \"C\" {static int convertTo_%L(PyObject *, void **, int *, PyObject *%s);}\n"
                , iff, (need_us_arg ? ", void **" : ""));

        prcode(fp,
"static int convertTo_%L(PyObject *%s, void **%s, int *%s, PyObject *%s"
            , iff, (need_py ? "sipPy" : ""), (need_ptr ? "sipCppPtrV" : ""), (need_iserr ? "sipIsErr" : ""), (need_xfer ? "sipTransferObj" : ""));

        if (need_us_arg)
            prcode(fp, ", void **%s", (need_us_val ? "sipUserStatePtr" : ""));

        prcode(fp, ")\n"
"{\n"
            );

        if (need_ptr)
        {
            if (generating_c)
                prcode(fp,
"    %b **sipCppPtr = (%b **)sipCppPtrV;\n"
"\n"
                    , &type, &type);
            else
                prcode(fp,
"    %b **sipCppPtr = reinterpret_cast<%b **>(sipCppPtrV);\n"
"\n"
                    , &type, &type);
        }

        generateCppCodeBlock(convtocode,fp);

        prcode(fp,
"}\n"
            );
    }
}


/*
 * Generate a variable getter.
 */
static void generateVariableGetter(ifaceFileDef *scope, varDef *vd, FILE *fp)
{
    argType atype = vd->type.atype;
    const char *first_arg, *second_arg, *last_arg;
    int needsNew, var_key, self_key;

    if (generating_c || !isStaticVar(vd))
        first_arg = "sipSelf";
    else
        first_arg = "";

    last_arg = (generating_c || usedInCode(vd->getcode, "sipPyType")) ? "sipPyType" : "";

    needsNew = ((atype == class_type || atype == mapped_type) && vd->type.nrderefs == 0 && isConstArg(&vd->type));

    /*
     * If the variable is itself a non-const instance of a wrapped class then
     * two things must happen.  Firstly, the getter must return the same Python
     * object each time - it must not re-wrap the the instance.  This is
     * because the Python object can contain important state information that
     * must not be lost (specifically references to other Python objects that
     * it owns).  Therefore the Python object wrapping the containing class
     * must cache a reference to the Python object wrapping the variable.
     * Secondly, the Python object wrapping the containing class must not be
     * garbage collected before the Python object wrapping the variable is
     * (because the latter references memory, ie. the variable itself, that is
     * managed by the former).  Therefore the Python object wrapping the
     * variable must keep a reference to the Python object wrapping the
     * containing class (but only if the latter is non-static).
     */
    var_key = self_key = 0;

    if (atype == class_type && vd->type.nrderefs == 0 && !isConstArg(&vd->type))
    {
        var_key = vd->type.u.cd->iff->module->next_key--;

        if (!isStaticVar(vd))
            self_key = vd->module->next_key--;
    }

    second_arg = (generating_c || var_key < 0 ) ? "sipPySelf" : "";

    prcode(fp,
"\n"
"\n"
        );

    if (!generating_c)
        prcode(fp,
"extern \"C\" {static PyObject *varget_%C(void *, PyObject *, PyObject *);}\n"
            , vd->fqcname);

    prcode(fp,
"static PyObject *varget_%C(void *%s, PyObject *%s, PyObject *%s)\n"
"{\n"
        , vd->fqcname, first_arg, second_arg, last_arg);

    if (vd->getcode != NULL)
    {
        prcode(fp,
"    PyObject *sipPy;\n"
            );
    }
    else if (var_key < 0)
    {
        if (isStaticVar(vd))
            prcode(fp,
"    static PyObject *sipPy = SIP_NULLPTR;\n"
                );
        else
            prcode(fp,
"    PyObject *sipPy;\n"
                );
    }

    if (vd->getcode == NULL)
    {
        prcode(fp,
"    ");

        generateNamedValueType(scope, &vd->type, "sipVal", fp);

        prcode(fp, ";\n"
            );
    }

    if (!isStaticVar(vd))
    {
        if (generating_c)
            prcode(fp,
"    %U *sipCpp = (%U *)sipSelf;\n"
                , vd->ecd, vd->ecd);
        else
            prcode(fp,
"    %U *sipCpp = reinterpret_cast<%U *>(sipSelf);\n"
                , vd->ecd, vd->ecd);
    }

    prcode(fp,
"\n"
        );

    /* Handle any handwritten getter. */
    if (vd->getcode != NULL)
    {
        generateCppCodeBlock(vd->getcode, fp);

        prcode(fp,
"\n"
"    return sipPy;\n"
"}\n"
            );

        return;
    }

    /* Get any previously wrapped cached object. */
    if (var_key < 0)
    {
        if (isStaticVar(vd))
            prcode(fp,
"    if (sipPy)\n"
"    {\n"
"        Py_INCREF(sipPy);\n"
"        return sipPy;\n"
"    }\n"
"\n"
                );
        else
            prcode(fp,
"    sipPy = sipGetReference(sipPySelf, %d);\n"
"\n"
"    if (sipPy)\n"
"        return sipPy;\n"
"\n"
                , self_key);
    }

    if (needsNew)
    {
        if (generating_c)
            prcode(fp,
"    *sipVal = ");
        else
            prcode(fp,
"    sipVal = new %b(", &vd->type);
    }
    else
    {
        prcode(fp,
"    sipVal = ");

        if ((atype == class_type || atype == mapped_type) && vd->type.nrderefs == 0)
            prcode(fp, "&");
    }

    generateVarMember(vd, fp);

    prcode(fp, "%s;\n"
"\n"
        , ((needsNew && !generating_c) ? ")" : ""));

    switch (atype)
    {
    case mapped_type:
    case class_type:
        {
            ifaceFileDef *iff;

            if (atype == mapped_type)
                iff = vd->type.u.mtd->iff;
            else
                iff = vd->type.u.cd->iff;

            prcode(fp,
"    %s sipConvertFrom%sType(", (var_key < 0 ? "sipPy =" : "return"), (needsNew ? "New" : ""));

            if (isConstArg(&vd->type))
                prcode(fp, "const_cast<%b *>(sipVal)", &vd->type);
            else
                prcode(fp, "sipVal");

            prcode(fp, ", sipType_%C, SIP_NULLPTR);\n"
                , iff->fqcname);

            if (var_key < 0)
            {
                prcode(fp,
"\n"
"    if (sipPy)\n"
"    {\n"
"        sipKeepReference(sipPy, %d, sipPySelf);\n"
                    , var_key);

                if (isStaticVar(vd))
                    prcode(fp,
"        Py_INCREF(sipPy);\n"
                        );
                else
                    prcode(fp,
"        sipKeepReference(sipPySelf, %d, sipPy);\n"
                        , self_key);

                prcode(fp,
"    }\n"
"\n"
"    return sipPy;\n"
                    );
            }
        }

        break;

    case bool_type:
    case cbool_type:
        prcode(fp,
"    return PyBool_FromLong(sipVal);\n"
            );

        break;

    case ascii_string_type:
        if (vd->type.nrderefs == 0)
            prcode(fp,
"    return PyUnicode_DecodeASCII(&sipVal, 1, SIP_NULLPTR);\n"
                );
        else
            prcode(fp,
"    if (sipVal == SIP_NULLPTR)\n"
"    {\n"
"        Py_INCREF(Py_None);\n"
"        return Py_None;\n"
"    }\n"
"\n"
"    return PyUnicode_DecodeASCII(sipVal, strlen(sipVal), SIP_NULLPTR);\n"
                );

        break;

    case latin1_string_type:
        if (vd->type.nrderefs == 0)
            prcode(fp,
"    return PyUnicode_DecodeLatin1(&sipVal, 1, SIP_NULLPTR);\n"
                );
        else
            prcode(fp,
"    if (sipVal == SIP_NULLPTR)\n"
"    {\n"
"        Py_INCREF(Py_None);\n"
"        return Py_None;\n"
"    }\n"
"\n"
"    return PyUnicode_DecodeLatin1(sipVal, strlen(sipVal), SIP_NULLPTR);\n"
                );

        break;

    case utf8_string_type:
        if (vd->type.nrderefs == 0)
            prcode(fp,
"    return PyUnicode_FromStringAndSize(&sipVal, 1);\n"
                );
        else
            prcode(fp,
"    if (sipVal == SIP_NULLPTR)\n"
"    {\n"
"        Py_INCREF(Py_None);\n"
"        return Py_None;\n"
"    }\n"
"\n"
"    return PyUnicode_FromString(sipVal);\n"
                );

        break;

    case sstring_type:
    case ustring_type:
    case string_type:
        {
            const char *cast = ((atype != string_type) ? "(char *)" : "");

            if (vd->type.nrderefs == 0)
                prcode(fp,
"    return PyBytes_FromStringAndSize(%s&sipVal, 1);\n"
                    , cast);
            else
                prcode(fp,
"    if (sipVal == SIP_NULLPTR)\n"
"    {\n"
"        Py_INCREF(Py_None);\n"
"        return Py_None;\n"
"    }\n"
"\n"
"    return PyBytes_FromString(%ssipVal);\n"
                    , cast);
        }

        break;

    case wstring_type:
        if (vd->type.nrderefs == 0)
            prcode(fp,
"    return PyUnicode_FromWideChar(&sipVal, 1);\n"
                );
        else
            prcode(fp,
"    if (sipVal == SIP_NULLPTR)\n"
"    {\n"
"        Py_INCREF(Py_None);\n"
"        return Py_None;\n"
"    }\n"
"\n"
"    return PyUnicode_FromWideChar(sipVal, (Py_ssize_t)wcslen(sipVal));\n"
                );

        break;

    case float_type:
    case cfloat_type:
        prcode(fp,
"    return PyFloat_FromDouble((double)sipVal);\n"
                );
        break;

    case double_type:
    case cdouble_type:
        prcode(fp,
"    return PyFloat_FromDouble(sipVal);\n"
            );
        break;

    case enum_type:
        if (vd->type.u.ed->fqcname != NULL)
        {
            const char *cast_prefix, *cast_suffix;

            if (generating_c)
            {
                cast_prefix = cast_suffix = "";
            }
            else
            {
                cast_prefix = "static_cast<int>(";
                cast_suffix = ")";
            }

            prcode(fp,
"    return sipConvertFromEnum(%ssipVal%s, sipType_%C);\n"
                , cast_prefix, cast_suffix, vd->type.u.ed->fqcname);

            break;
        }

        /* Drop through. */

    case byte_type:
    case sbyte_type:
    case short_type:
    case cint_type:
    case int_type:
        prcode(fp,
"    return PyLong_FromLong(sipVal);\n"
            );
        break;

    case long_type:
        prcode(fp,
"    return PyLong_FromLong(sipVal);\n"
            );
        break;

    case ubyte_type:
    case ushort_type:
        prcode(fp,
"    return PyLong_FromUnsignedLong(sipVal);\n"
            );
        break;

    case uint_type:
    case ulong_type:
    case size_type:
        prcode(fp,
"    return PyLong_FromUnsignedLong(sipVal);\n"
            );
        break;

    case longlong_type:
        prcode(fp,
"    return PyLong_FromLongLong(sipVal);\n"
            );
        break;

    case ulonglong_type:
        prcode(fp,
"    return PyLong_FromUnsignedLongLong(sipVal);\n"
            );
        break;

    case struct_type:
    case union_type:
    case void_type:
        prcode(fp,
"    return sipConvertFrom%sVoidPtr(", (isConstArg(&vd->type) ? "Const" : ""));
        generateVoidPtrCast(&vd->type, fp);
        prcode(fp, "sipVal);\n");
        break;

    case capsule_type:
        prcode(fp,
"    return PyCapsule_New(");
        generateVoidPtrCast(&vd->type, fp);
        prcode(fp, "sipVal, \"%S\", SIP_NULLPTR);\n"
            , vd->type.u.cap);
        break;

    case pyobject_type:
    case pytuple_type:
    case pylist_type:
    case pydict_type:
    case pycallable_type:
    case pyslice_type:
    case pytype_type:
    case pybuffer_type:
    case pyenum_type:
        prcode(fp,
"    Py_XINCREF(sipVal);\n"
"    return sipVal;\n"
            );
        break;

    /* Supress a compiler warning. */
    default:
        ;
    }

    prcode(fp,
"}\n"
        );
}


/*
 * Generate a variable setter.
 */
static void generateVariableSetter(ifaceFileDef *scope, varDef *vd, FILE *fp)
{
    argType atype = vd->type.atype;
    const char *first_arg, *last_arg, *error_test;
    char *deref;
    int has_state, keep, need_py, need_cpp;

    /*
     * We need to keep a reference to the original Python object if it
     * providing the memory that the C/C++ variable is pointing to.
     */
    keep = keepPyReference(&vd->type);

    if (generating_c || !isStaticVar(vd))
        first_arg = "sipSelf";
    else
        first_arg = "";

    if (generating_c || (!isStaticVar(vd) && keep))
        last_arg = "sipPySelf";
    else
        last_arg = "";

    need_py = (generating_c || vd->setcode == NULL || usedInCode(vd->setcode, "sipPy"));
    need_cpp = (generating_c || vd->setcode == NULL || usedInCode(vd->setcode, "sipCpp"));

    prcode(fp,
"\n"
"\n"
        );

    if (!generating_c)
        prcode(fp,
"extern \"C\" {static int varset_%C(void *, PyObject *, PyObject *);}\n"
            , vd->fqcname);

    prcode(fp,
"static int varset_%C(void *%s, PyObject *%s, PyObject *%s)\n"
"{\n"
        , vd->fqcname, (need_cpp ? first_arg : ""), (need_py ? "sipPy" : ""), last_arg);

    if (vd->setcode == NULL)
    {
        prcode(fp,
"    ");

        if (atype == bool_type)
            prcode(fp, "int sipVal");
        else
            generateNamedValueType(scope, &vd->type, "sipVal", fp);

        prcode(fp, ";\n"
            );
    }

    if (!isStaticVar(vd) && need_cpp)
    {
        if (generating_c)
            prcode(fp,
"    %U *sipCpp = (%U *)sipSelf;\n"
                , vd->ecd, vd->ecd);
        else
            prcode(fp,
"    %U *sipCpp = reinterpret_cast<%U *>(sipSelf);\n"
                , vd->ecd, vd->ecd);

        prcode(fp,
"\n"
            );
    }

    /* Handle any handwritten setter. */
    if (vd->setcode != NULL)
    {
        prcode(fp,
"   int sipErr = 0;\n"
"\n"
            );

        generateCppCodeBlock(vd->setcode, fp);

        prcode(fp,
"\n"
"    return (sipErr ? -1 : 0);\n"
"}\n"
            );

        return;
    }

    has_state = FALSE;

    if (atype == class_type || atype == mapped_type)
    {
        prcode(fp,
"    int sipIsErr = 0;\n"
            );

        if (vd->type.nrderefs == 0)
        {
            codeBlockList *convtocode;

            if (atype == class_type)
                convtocode = vd->type.u.cd->convtocode;
            else if (noRelease(vd->type.u.mtd))
                convtocode = NULL;
            else
                convtocode = vd->type.u.mtd->convtocode;

            if (convtocode != NULL)
            {
                has_state = TRUE;

                prcode(fp,
"    int sipValState;\n"
                    );

                if (typeNeedsUserState(&vd->type))
                    prcode(fp,
"    void *sipValUserState;\n"
                        );
            }
        }
    }

    generateObjToCppConversion(&vd->type, has_state, fp);

    deref = "";

    if (atype == class_type || atype == mapped_type)
    {
        if (vd->type.nrderefs == 0)
            deref = "*";

        error_test = "sipIsErr";
    }
    else if (atype == bool_type)
    {
        error_test = "sipVal < 0";
    }
    else
    {
        error_test = "PyErr_Occurred() != SIP_NULLPTR";
    }

    prcode(fp,
"\n"
"    if (%s)\n"
"        return -1;\n"
"\n"
        , error_test);

    if (atype == pyobject_type || atype == pytuple_type ||
        atype == pylist_type || atype == pydict_type ||
        atype == pycallable_type || atype == pyslice_type ||
        atype == pytype_type || atype == pybuffer_type || atype == pyenum_type)
    {
        prcode(fp,
"    Py_XDECREF(");

        generateVarMember(vd, fp);

        prcode(fp, ");\n"
"    Py_INCREF(sipVal);\n"
"\n"
            );
    }

    prcode(fp,
"    ");

    generateVarMember(vd, fp);

    if (atype == bool_type)
    {
        if (generating_c)
            prcode(fp, " = (bool)%ssipVal;\n"
                , deref);
        else
            prcode(fp, " = static_cast<bool>(%ssipVal);\n"
                , deref);
    }
    else
    {
        prcode(fp, " = %ssipVal;\n"
            , deref);
    }

    /* Note that wchar_t * leaks here. */

    if (has_state)
    {
        prcode(fp,
"\n"
"    sipReleaseType%s(sipVal, sipType_%T, sipValState", userStateSuffix(&vd->type), &vd->type);

        if (typeNeedsUserState(&vd->type))
            prcode(fp, ", sipValUserState");

        prcode(fp, ");\n"
            );
    }

    /* Generate the code to keep the object alive while we use its data. */
    if (keep)
    {
        if (isStaticVar(vd))
        {
            prcode(fp,
"\n"
"    static PyObject *sipKeep = SIP_NULLPTR;\n"
"\n"
"    Py_XDECREF(sipKeep);\n"
"    sipKeep = sipPy;\n"
"    Py_INCREF(sipKeep);\n"
                );
        }
        else
        {
            prcode(fp,
"\n"
"    sipKeepReference(sipPySelf, %d, sipPy);\n"
                , scope->module->next_key--);
        }
    }

    prcode(fp,
"\n"
"    return 0;\n"
"}\n"
        );
}


/*
 * Generate the member variable of a class.
 */
static void generateVarMember(varDef *vd, FILE *fp)
{
    if (isStaticVar(vd))
        prcode(fp, "%S::", classFQCName(vd->ecd));
    else
        prcode(fp, "sipCpp->");

    prcode(fp, "%s", scopedNameTail(vd->fqcname));
}


/*
 * Generate the declaration of a variable that is initialised from a Python
 * object.
 */
static void generateObjToCppConversion(argDef *ad, int has_state, FILE *fp)
{
    char *rhs = NULL;

    prcode(fp,
"    sipVal = ");

    switch (ad->atype)
    {
    case class_type:
    case mapped_type:
        {
            const char *tail;

            if (generating_c)
            {
                prcode(fp, "(%b *)", ad);
                tail = "";
            }
            else
            {
                prcode(fp, "reinterpret_cast<%b *>(", ad);
                tail = ")";
            }

            /*
             * Note that we don't support /Transfer/ but could do.  We could
             * also support /Constrained/ (so long as we also supported it for
             * all types).
             */

            prcode(fp, "sipForceConvertToType%s(sipPy, sipType_%T, SIP_NULLPTR, %s, %s", userStateSuffix(ad), ad, (ad->nrderefs ? "0" : "SIP_NOT_NONE"), (has_state ? "&sipValState" : "SIP_NULLPTR"));

            if (typeNeedsUserState(ad))
                prcode(fp, ", &sipValUserState");

            prcode(fp, ", &sipIsErr)%s;\n"
                , tail);
        }
        break;

    case enum_type:
        prcode(fp, "(%E)sipConvertToEnum(sipPy, sipType_%C);\n"
            , ad->u.ed, ad->u.ed->fqcname);
        break;

    case sstring_type:
        if (ad->nrderefs == 0)
            rhs = "(signed char)sipBytes_AsChar(sipPy)";
        else if (isConstArg(ad))
            rhs = "(const signed char *)sipBytes_AsString(sipPy)";
        else
            rhs = "(signed char *)sipBytes_AsString(sipPy)";
        break;

    case ustring_type:
        if (ad->nrderefs == 0)
            rhs = "(unsigned char)sipBytes_AsChar(sipPy)";
        else if (isConstArg(ad))
            rhs = "(const unsigned char *)sipBytes_AsString(sipPy)";
        else
            rhs = "(unsigned char *)sipBytes_AsString(sipPy)";
        break;

    case ascii_string_type:
        if (ad->nrderefs == 0)
            rhs = "sipString_AsASCIIChar(sipPy)";
        else if (isConstArg(ad))
            rhs = "sipString_AsASCIIString(&sipPy)";
        else
            rhs = "(char *)sipString_AsASCIIString(&sipPy)";
        break;

    case latin1_string_type:
        if (ad->nrderefs == 0)
            rhs = "sipString_AsLatin1Char(sipPy)";
        else if (isConstArg(ad))
            rhs = "sipString_AsLatin1String(&sipPy)";
        else
            rhs = "(char *)sipString_AsLatin1String(&sipPy)";
        break;

    case utf8_string_type:
        if (ad->nrderefs == 0)
            rhs = "sipString_AsUTF8Char(sipPy)";
        else if (isConstArg(ad))
            rhs = "sipString_AsUTF8String(&sipPy)";
        else
            rhs = "(char *)sipString_AsUTF8String(&sipPy)";
        break;

    case string_type:
        if (ad->nrderefs == 0)
            rhs = "sipBytes_AsChar(sipPy)";
        else if (isConstArg(ad))
            rhs = "sipBytes_AsString(sipPy)";
        else
            rhs = "(char *)sipBytes_AsString(sipPy)";
        break;

    case wstring_type:
        if (ad->nrderefs == 0)
            rhs = "sipUnicode_AsWChar(sipPy)";
        else
            rhs = "sipUnicode_AsWString(sipPy)";
        break;

    case float_type:
    case cfloat_type:
        rhs = "(float)PyFloat_AsDouble(sipPy)";
        break;

    case double_type:
    case cdouble_type:
        rhs = "PyFloat_AsDouble(sipPy)";
        break;

    case bool_type:
    case cbool_type:
        rhs = "sipConvertToBool(sipPy)";
        break;

    case byte_type:
        rhs = "sipLong_AsChar(sipPy)";
        break;

    case sbyte_type:
        rhs = "sipLong_AsSignedChar(sipPy)";
        break;

    case ubyte_type:
        rhs = "sipLong_AsUnsignedChar(sipPy)";
        break;

    case ushort_type:
        rhs = "sipLong_AsUnsignedShort(sipPy)";
        break;

    case short_type:
        rhs = "sipLong_AsShort(sipPy)";
        break;

    case uint_type:
        rhs = "sipLong_AsUnsignedInt(sipPy)";
        break;

    case size_type:
        rhs = "sipLong_AsSizeT(sipPy)";
        break;

    case int_type:
    case cint_type:
        rhs = "sipLong_AsInt(sipPy)";
        break;

    case ulong_type:
        rhs = "sipLong_AsUnsignedLong(sipPy)";
        break;

    case long_type:
        rhs = "sipLong_AsLong(sipPy)";
        break;

    case ulonglong_type:
        rhs = "sipLong_AsUnsignedLongLong(sipPy)";
        break;

    case longlong_type:
        rhs = "sipLong_AsLongLong(sipPy)";
        break;

    case struct_type:
        prcode(fp, "(struct %S *)sipConvertToVoidPtr(sipPy);\n"
            , ad->u.sname);
        break;

    case union_type:
        prcode(fp, "(union %S *)sipConvertToVoidPtr(sipPy);\n"
            , ad->u.sname);
        break;

    case void_type:
        rhs = "sipConvertToVoidPtr(sipPy)";
        break;

    case capsule_type:
        prcode(fp, "PyCapsule_GetPointer(sipPy, \"%S\");\n"
            , ad->u.cap);
        break;

    case pyobject_type:
    case pytuple_type:
    case pylist_type:
    case pydict_type:
    case pycallable_type:
    case pyslice_type:
    case pytype_type:
    case pybuffer_type:
    case pyenum_type:
        rhs = "sipPy";
        break;

    /* Supress a compiler warning. */
    default:
        ;
    }

    if (rhs != NULL)
        prcode(fp, "%s;\n"
            , rhs);
}


/*
 * Returns TRUE if the given method is a slot that takes zero arguments.
 */
static int isZeroArgSlot(memberDef *md)
{
    slotType st = md->slot;

    return (st == str_slot || st == int_slot || st == float_slot ||
        st == invert_slot || st == neg_slot || st == len_slot ||
        st == bool_slot || st == pos_slot || st == abs_slot ||
        st == repr_slot || st == hash_slot || st == index_slot ||
        st == iter_slot || st == next_slot || st == await_slot ||
        st == aiter_slot || st == anext_slot);
}


/*
 * Returns TRUE if the given method is a slot that takes more than one
 * argument.
 */
static int isMultiArgSlot(memberDef *md)
{
    slotType st = md->slot;

    return (st == setitem_slot || st == call_slot);
}


/*
 * Returns TRUE if the given method is a slot that returns void (ie. nothing
 * other than an error indicator).
 */
static int isVoidReturnSlot(memberDef *md)
{
    slotType st = md->slot;

    return (st == setitem_slot || st == delitem_slot || st == setattr_slot);
}


/*
 * Returns TRUE if the given method is a slot that returns int.
 */
static int isIntReturnSlot(memberDef *md)
{
    slotType st = md->slot;

    return (st == bool_slot || st == contains_slot || st == cmp_slot);
}


/*
 * Returns TRUE if the given method is a slot that returns Py_ssize_t.
 */
static int isSSizeReturnSlot(memberDef *md)
{
    slotType st = md->slot;

    return (st == len_slot);
}


/*
 * Returns TRUE if the given method is a slot that returns Py_hash_t.
 */
static int isHashReturnSlot(memberDef *md)
{
    slotType st = md->slot;

    return (st == hash_slot);
}


/*
 * Returns TRUE if the given method is a slot that takes an int argument.
 */
static int isIntArgSlot(memberDef *md)
{
    slotType st = md->slot;

    return (st == repeat_slot || st == irepeat_slot);
}


/*
 * Returns TRUE if the given method is an inplace number slot.
 */
static int isInplaceNumberSlot(memberDef *md)
{
    slotType st = md->slot;

    return (st == iadd_slot || st == isub_slot || st == imul_slot ||
        st == imod_slot || st == ifloordiv_slot || st == itruediv_slot ||
        st == ior_slot || st == ixor_slot || st == iand_slot ||
        st == ilshift_slot || st == irshift_slot || st == imatmul_slot);
}


/*
 * Returns TRUE if the given method is an inplace sequence slot.
 */
static int isInplaceSequenceSlot(memberDef *md)
{
    slotType st = md->slot;

    return (st == iconcat_slot || st == irepeat_slot);
}


/*
 * Returns TRUE if the given method is a number slot.
 */
int isNumberSlot(memberDef *md)
{
    slotType st = md->slot;

    return (st == add_slot || st == sub_slot || st == mul_slot ||
        st == mod_slot || st == floordiv_slot || st == truediv_slot ||
        st == and_slot || st == or_slot || st == xor_slot ||
        st == lshift_slot || st == rshift_slot || st == matmul_slot);
}


/*
 * Returns TRUE if the given method is a rich compare slot.
 */
static int isRichCompareSlot(memberDef *md)
{
    slotType st = md->slot;

    return (st == lt_slot || st == le_slot || st == eq_slot ||
        st == ne_slot || st == gt_slot || st == ge_slot);
}


/*
 * Generate a Python slot handler for either a class, an enum or an extender.
 */
static int generateSlot(moduleDef *mod, classDef *cd, enumDef *ed,
        memberDef *md, FILE *fp)
{
    char *arg_str, *decl_arg_str, *prefix, *ret_type, *ret_value;
    int has_args;
    overDef *od, *overs;
    scopedNameDef *fqcname;
    nameDef *pyname;

    if (ed != NULL)
    {
        prefix = "Type";
        pyname = ed->pyname;
        fqcname = ed->fqcname;
        overs = ed->overs;
    }
    else if (cd != NULL)
    {
        prefix = "Type";
        pyname = cd->pyname;
        fqcname = classFQCName(cd);
        overs = cd->overs;
    }
    else
    {
        prefix = NULL;
        pyname = NULL;
        fqcname = NULL;
        overs = mod->overs;
    }

    if (isVoidReturnSlot(md) || isIntReturnSlot(md))
    {
        ret_type = "int ";
        ret_value = "-1";
    }
    else if (isSSizeReturnSlot(md))
    {
        ret_type = "Py_ssize_t ";
        ret_value = "0";
    }
    else if (isHashReturnSlot(md))
    {
        if (abiVersion >= ABI_13_0)
        {
            ret_type = "Py_hash_t ";
            ret_value = "0";
        }
        else
        {
            ret_type = "long ";
            ret_value = "0L";
        }
    }
    else
    {
        ret_type = "PyObject *";
        ret_value = "SIP_NULLPTR";
    }

    has_args = TRUE;

    if (isIntArgSlot(md))
    {
        has_args = FALSE;
        arg_str = "PyObject *sipSelf, int a0";
        decl_arg_str = "PyObject *, int";
    }
    else if (md->slot == call_slot)
    {
        if (generating_c || useKeywordArgs(md) || noArgParser(md))
            arg_str = "PyObject *sipSelf, PyObject *sipArgs, PyObject *sipKwds";
        else
            arg_str = "PyObject *sipSelf, PyObject *sipArgs, PyObject *";

        decl_arg_str = "PyObject *, PyObject *, PyObject *";
    }
    else if (isMultiArgSlot(md))
    {
        arg_str = "PyObject *sipSelf, PyObject *sipArgs";
        decl_arg_str = "PyObject *, PyObject *";
    }
    else if (isZeroArgSlot(md))
    {
        has_args = FALSE;
        arg_str = "PyObject *sipSelf";
        decl_arg_str = "PyObject *";
    }
    else if (isNumberSlot(md))
    {
        arg_str = "PyObject *sipArg0, PyObject *sipArg1";
        decl_arg_str = "PyObject *, PyObject *";
    }
    else if (md->slot == setattr_slot)
    {
        arg_str = "PyObject *sipSelf, PyObject *sipName, PyObject *sipValue";
        decl_arg_str = "PyObject *, PyObject *, PyObject *";
    }
    else
    {
        arg_str = "PyObject *sipSelf, PyObject *sipArg";
        decl_arg_str = "PyObject *, PyObject *";
    }

    prcode(fp,
"\n"
"\n"
        );

    if (!generating_c)
    {
        prcode(fp,
"extern \"C\" {static %sslot_", ret_type);

        if (cd != NULL)
            prcode(fp, "%L_", cd->iff);
        else if (fqcname != NULL)
            prcode(fp, "%C_", fqcname);

        prcode(fp, "%s(%s);}\n"
            , md->pyname->text, decl_arg_str);
    }

    prcode(fp,
"static %sslot_", ret_type);

    if (cd != NULL)
        prcode(fp, "%L_", cd->iff);
    else if (fqcname != NULL)
        prcode(fp, "%C_", fqcname);

    prcode(fp, "%s(%s)\n"
"{\n"
        , md->pyname->text, arg_str);

    if (md->slot == call_slot && noArgParser(md))
    {
        for (od = overs; od != NULL; od = od->next)
            if (od->common == md)
                generateCppCodeBlock(od->methodcode, fp);
    }
    else
    {
        if (isInplaceNumberSlot(md))
            prcode(fp,
"    if (!PyObject_TypeCheck(sipSelf, sipTypeAsPyTypeObject(sip%s_%C)))\n"
"    {\n"
"        Py_INCREF(Py_NotImplemented);\n"
"        return Py_NotImplemented;\n"
"    }\n"
"\n"
                , prefix, fqcname);

        if (!isNumberSlot(md))
        {
            if (cd != NULL)
                prcode(fp,
"    %S *sipCpp = reinterpret_cast<%S *>(sipGetCppPtr((sipSimpleWrapper *)sipSelf, sipType_%C));\n"
"\n"
"    if (!sipCpp)\n"
                    , fqcname, fqcname, fqcname);
            else
                prcode(fp,
"    %S sipCpp = static_cast<%S>(sipConvertToEnum(sipSelf, sipType_%C));\n"
"\n"
"    if (PyErr_Occurred())\n"
                    , fqcname, fqcname, fqcname);

            prcode(fp,
"        return %s;\n"
"\n"
                , (md->slot == cmp_slot ? "-2" : ret_value));
        }

        if (has_args)
            prcode(fp,
"    PyObject *sipParseErr = SIP_NULLPTR;\n"
                );

        for (od = overs; od != NULL; od = od->next)
            if (od->common == md && isAbstract(od))
            {
                prcode(fp,
"    PyObject *sipOrigSelf = sipSelf;\n"
                    );

                break;
            }

        for (od = overs; od != NULL; od = od->next)
            if (od->common == md)
                if (generateFunctionBody(od, cd, NULL, cd, (ed == NULL && !dontDerefSelf(od)), mod, fp) < 0)
                    return -1;

        if (has_args)
        {
            switch (md->slot)
            {
            case cmp_slot:
                prcode(fp,
"\n"
"    return 2;\n"
                    );
                break;

            case concat_slot:
            case iconcat_slot:
            case repeat_slot:
            case irepeat_slot:
                prcode(fp,
"\n"
"    /* Raise an exception if the argument couldn't be parsed. */\n"
"    sipBadOperatorArg(sipSelf, sipArg, %s);\n"
"\n"
"    return SIP_NULLPTR;\n"
                    ,slotName(md->slot));
                break;

            default:
                if (isRichCompareSlot(md))
                {
                    prcode(fp,
"\n"
"    Py_XDECREF(sipParseErr);\n"
                        );
                }
                else if (isNumberSlot(md) || isInplaceNumberSlot(md))
                {
                    prcode(fp,
"\n"
"    Py_XDECREF(sipParseErr);\n"
"\n"
"    if (sipParseErr == Py_None)\n"
"        return SIP_NULLPTR;\n"
                        );
                }

                if (isNumberSlot(md) || isRichCompareSlot(md))
                {
                    /* We can't extend enum slots. */
                    if (cd == NULL)
                        prcode(fp,
"\n"
"    PyErr_Clear();\n"
"\n"
"    Py_INCREF(Py_NotImplemented);\n"
"    return Py_NotImplemented;\n"
                            );
                    else if (isNumberSlot(md))
                        prcode(fp,
"\n"
"    return sipPySlotExtend(&sipModuleAPI_%s, %s, SIP_NULLPTR, sipArg0, sipArg1);\n"
                            , mod->name, slotName(md->slot));
                    else
                        prcode(fp,
"\n"
"    return sipPySlotExtend(&sipModuleAPI_%s, %s, sipType_%C, sipSelf, sipArg);\n"
                            , mod->name, slotName(md->slot), fqcname);
                }
                else if (isInplaceNumberSlot(md))
                {
                    prcode(fp,
"\n"
"    PyErr_Clear();\n"
"\n"
"    Py_INCREF(Py_NotImplemented);\n"
"    return Py_NotImplemented;\n"
                        );
                }
                else
                {
                    prcode(fp,
"\n"
"    sipNoMethod(sipParseErr, %N, ", pyname);

                    if (md->slot == setattr_slot)
                        prcode(fp, "(sipValue != SIP_NULLPTR ? sipName___setattr__ : sipName___delattr__)");
                    else
                        prcode(fp, "%N", md->pyname);

                    prcode(fp, ", SIP_NULLPTR);\n"
"\n"
"    return %s;\n"
                        , ret_value);
                }
            }
        }
        else
        {
            prcode(fp,
"\n"
"    return 0;\n"
                );
        }
    }

    prcode(fp,
"}\n"
        );

    return 0;
}


/*
 * Generate the member functions for a class.
 */
static int generateClassFunctions(sipSpec *pt, moduleDef *mod, classDef *cd,
        int py_debug, FILE *fp)
{
    visibleList *vl;
    memberDef *md;

    /* Any shadow code. */
    if (hasShadow(cd))
    {
        if (!isExportDerived(cd))
            generateShadowClassDeclaration(pt, cd, fp);

        if (generateShadowCode(pt, mod, cd, fp) < 0)
            return -1;
    }

    /* The member functions. */
    for (vl = cd->visible; vl != NULL; vl = vl->next)
        if (vl->m->slot == no_slot)
            if (generateFunction(pt, vl->m, vl->cd->overs, cd, vl->cd, mod, fp) < 0)
                return -1;

    /* The slot functions. */
    for (md = cd->members; md != NULL; md = md->next)
        if (cd->iff->type == namespace_iface)
        {
            if (generateOrdinaryFunction(pt, mod, cd, NULL, md, fp) < 0)
                return -1;
        }
        else if (md->slot != no_slot)
        {
            if (generateSlot(mod, cd, NULL, md, fp) < 0)
                return -1;
        }

    /* The cast function. */
    if (cd->supers != NULL)
    {
        classList *super;

        prcode(fp,
"\n"
"\n"
"/* Cast a pointer to a type somewhere in its inheritance hierarchy. */\n"
"extern \"C\" {static void *cast_%L(void *, const sipTypeDef *);}\n"
"static void *cast_%L(void *sipCppV, const sipTypeDef *targetType)\n"
"{\n"
"    "
            , cd->iff
            , cd->iff);

        generateClassFromVoid(cd, "sipCpp", "sipCppV", fp);

        prcode(fp, ";\n"
"\n"
"    if (targetType == sipType_%C)\n"
"        return sipCppV;\n"
"\n"
            , classFQCName(cd));

        for (super = cd->supers; super != NULL; super = super->next)
        {
            if (super->cd->supers != NULL)
            {
                /*
                 * Delegate to the super-class's cast function.  This will
                 * handle virtual and non-virtual diamonds.
                 */
                prcode(fp,
"    sipCppV = ((const sipClassTypeDef *)sipType_%C)->ctd_cast(static_cast<%U *>(sipCpp), targetType);\n"
"    if (sipCppV)\n"
"        return sipCppV;\n"
"\n"
                    , classFQCName(super->cd)
                    , super->cd);
            }
            else
            {
                /*
                 * The super-class is a base class and so doesn't have a cast
                 * function.  It also means that a simple check will do
                 * instead.
                 */
                prcode(fp,
"    if (targetType == sipType_%C)\n"
"        return static_cast<%U *>(sipCpp);\n"
"\n"
                    , classFQCName(super->cd)
                    , super->cd);
            }
        }

        prcode(fp,
"    return SIP_NULLPTR;\n"
"}\n"
            );
    }

    if (cd->iff->type != namespace_iface && !generating_c)
    {
        int need_ptr = FALSE, need_cast_ptr = FALSE, need_state = FALSE;

        /* Generate the release function without compiler warnings. */

        if (cd->dealloccode != NULL)
            need_ptr = need_cast_ptr = usedInCode(cd->dealloccode, "sipCpp");

        if (canCreate(cd) || isPublicDtor(cd))
        {
            if ((pluginPyQt5(pt) || pluginPyQt6(pt)) && isQObjectSubClass(cd) && isPublicDtor(cd))
                need_ptr = need_cast_ptr = TRUE;
            else if (hasShadow(cd))
                need_ptr = need_state = TRUE;
            else if (isPublicDtor(cd))
                need_ptr = TRUE;
        }

        prcode(fp,
"\n"
"\n"
"/* Call the instance's destructor. */\n"
            );

        if (!generating_c)
            prcode(fp,
"extern \"C\" {static void release_%L(void *, int);}\n"
                , cd->iff);

        prcode(fp,
"static void release_%L(void *%s, int%s)\n"
"{\n"
            , cd->iff, ((generating_c || need_ptr) ? "sipCppV" : ""), ((generating_c || need_state) ? " sipState" : ""));

        if (need_cast_ptr)
        {
            prcode(fp,
"    ");

            generateClassFromVoid(cd, "sipCpp", "sipCppV", fp);

            prcode(fp, ";\n"
"\n"
                );
        }

        if (cd->dealloccode != NULL)
        {
            generateCppCodeBlock(cd->dealloccode, fp);

            prcode(fp,
"\n"
                );
        }

        if (canCreate(cd) || isPublicDtor(cd))
        {
            int rgil = ((release_gil || isReleaseGILDtor(cd)) && !isHoldGILDtor(cd));

            /*
             * If there is an explicit public dtor then assume there is some
             * way to call it which we haven't worked out (because we don't
             * fully understand C++).
             */

            if (rgil)
                prcode(fp,
"    Py_BEGIN_ALLOW_THREADS\n"
"\n"
                    );

            if ((pluginPyQt5(pt) || pluginPyQt6(pt)) && isQObjectSubClass(cd) && isPublicDtor(cd))
            {
                /*
                 * QObjects should only be deleted in the threads that they
                 * belong to.
                 */
                prcode(fp,
"    if (QThread::currentThread() == sipCpp->thread())\n"
"        delete sipCpp;\n"
"    else\n"
"        sipCpp->deleteLater();\n"
                        );
            }
            else if (hasShadow(cd))
            {
                prcode(fp,
"    if (sipState & SIP_DERIVED_CLASS)\n"
"        delete reinterpret_cast<sip%C *>(sipCppV);\n"
                    , classFQCName(cd));

                if (isPublicDtor(cd))
                    prcode(fp,
"    else\n"
"        delete reinterpret_cast<%U *>(sipCppV);\n"
                        , cd);
            }
            else if (isPublicDtor(cd))
                prcode(fp,
"    delete reinterpret_cast<%U *>(sipCppV);\n"
                    , cd);

            if (rgil)
                prcode(fp,
"\n"
"    Py_END_ALLOW_THREADS\n"
                    );
        }

        prcode(fp,
"}\n"
            );
    }

    /* The traverse function. */
    if (cd->travcode != NULL)
    {
        prcode(fp,
"\n"
"\n"
            );

        if (!generating_c)
            prcode(fp,
"extern \"C\" {static int traverse_%C(void *, visitproc, void *);}\n"
                , classFQCName(cd));

        prcode(fp,
"static int traverse_%C(void *sipCppV, visitproc sipVisit, void *sipArg)\n"
"{\n"
"    ", classFQCName(cd));

        generateClassFromVoid(cd, "sipCpp", "sipCppV", fp);

        prcode(fp, ";\n"
"    int sipRes;\n"
"\n"
            );

        generateCppCodeBlock(cd->travcode, fp);

        prcode(fp,
"\n"
"    return sipRes;\n"
"}\n"
            );
    }

    /* The clear function. */
    if (cd->clearcode != NULL)
    {
        prcode(fp,
"\n"
"\n"
            );

        if (!generating_c)
            prcode(fp,
"extern \"C\" {static int clear_%C(void *);}\n"
                , classFQCName(cd));

        prcode(fp,
"static int clear_%C(void *sipCppV)\n"
"{\n"
"    ", classFQCName(cd));

        generateClassFromVoid(cd, "sipCpp", "sipCppV", fp);

        prcode(fp, ";\n"
"    int sipRes;\n"
"\n"
            );

        generateCppCodeBlock(cd->clearcode, fp);

        prcode(fp,
"\n"
"    return sipRes;\n"
"}\n"
            );
    }

    /* The buffer interface functions. */
    if (cd->getbufcode != NULL)
    {
        int need_cpp = usedInCode(cd->getbufcode, "sipCpp");

        prcode(fp,
"\n"
"\n"
            );

        if (!py_debug && useLimitedAPI(mod))
        {
            if (!generating_c)
                prcode(fp,
"extern \"C\" {static int getbuffer_%C(PyObject *, void *, sipBufferDef *);}\n"
                    , classFQCName(cd));

            prcode(fp,
"static int getbuffer_%C(PyObject *%s, void *%s, sipBufferDef *sipBuffer)\n"
                , classFQCName(cd), argName("sipSelf", cd->getbufcode), (generating_c || need_cpp ? "sipCppV" : ""));
        }
        else
        {
            if (!generating_c)
                prcode(fp,
"extern \"C\" {static int getbuffer_%C(PyObject *, void *, Py_buffer *, int);}\n"
                    , classFQCName(cd));

            prcode(fp,
"static int getbuffer_%C(PyObject *%s, void *%s, Py_buffer *sipBuffer, int %s)\n"
                , classFQCName(cd), argName("sipSelf", cd->getbufcode), (generating_c || need_cpp ? "sipCppV" : ""), argName("sipFlags", cd->getbufcode));
        }

        prcode(fp,
"{\n"
            );

        if (need_cpp)
        {
            prcode(fp, "    ");
            generateClassFromVoid(cd, "sipCpp", "sipCppV", fp);
            prcode(fp, ";\n"
                );
        }

        prcode(fp,
"    int sipRes;\n"
"\n"
            );

        generateCppCodeBlock(cd->getbufcode, fp);

        prcode(fp,
"\n"
"    return sipRes;\n"
"}\n"
            );
    }

    if (cd->releasebufcode != NULL)
    {
        int need_cpp = usedInCode(cd->releasebufcode, "sipCpp");

        prcode(fp,
"\n"
"\n"
            );

        if (!py_debug && useLimitedAPI(mod))
        {
            if (!generating_c)
                prcode(fp,
"extern \"C\" {static void releasebuffer_%C(PyObject *, void *);}\n"
                    , classFQCName(cd));

            prcode(fp,
"static void releasebuffer_%C(PyObject *%s, void *%s)\n"
                , classFQCName(cd), argName("sipSelf", cd->releasebufcode), (generating_c || need_cpp ? "sipCppV" : ""));
        }
        else
        {
            if (!generating_c)
                prcode(fp,
"extern \"C\" {static void releasebuffer_%C(PyObject *, void *, Py_buffer *);}\n"
                    , classFQCName(cd));

            prcode(fp,
"static void releasebuffer_%C(PyObject *%s, void *%s, Py_buffer *%s)\n"
                , classFQCName(cd), argName("sipSelf", cd->releasebufcode), (generating_c || need_cpp ? "sipCppV" : ""), argName("sipBuffer", cd->releasebufcode));
        }

        prcode(fp,
"{\n"
            );

        if (need_cpp)
        {
            prcode(fp, "    ");
            generateClassFromVoid(cd, "sipCpp", "sipCppV", fp);
            prcode(fp, ";\n"
                );
        }

        generateCppCodeBlock(cd->releasebufcode, fp);

        prcode(fp,
"}\n"
            );
    }

    /* The pickle function. */
    if (cd->picklecode != NULL)
    {
        prcode(fp,
"\n"
"\n"
            );

        if (!generating_c)
            prcode(fp,
"extern \"C\" {static PyObject *pickle_%C(void *);}\n"
                , classFQCName(cd));

        prcode(fp,
"static PyObject *pickle_%C(void *sipCppV)\n"
"{\n"
"    ", classFQCName(cd));

        generateClassFromVoid(cd, "sipCpp", "sipCppV", fp);

        prcode(fp, ";\n"
"    PyObject *sipRes;\n"
"\n"
            );

        generateCppCodeBlock(cd->picklecode, fp);

        prcode(fp,
"\n"
"    return sipRes;\n"
"}\n"
            );
    }

    /* The finalisation function. */
    if (cd->finalcode != NULL)
    {
        int need_cpp = usedInCode(cd->finalcode, "sipCpp");

        prcode(fp,
"\n"
"\n"
            );

        if (!generating_c)
            prcode(fp,
"extern \"C\" {static int final_%C(PyObject *, void *, PyObject *, PyObject **);}\n"
                , classFQCName(cd));

        prcode(fp,
"static int final_%C(PyObject *%s, void *%s, PyObject *%s, PyObject **%s)\n"
"{\n"
            , classFQCName(cd), (usedInCode(cd->finalcode, "sipSelf") ? "sipSelf" : ""), (need_cpp ? "sipCppV" : ""), (usedInCode(cd->finalcode, "sipKwds") ? "sipKwds" : ""), (usedInCode(cd->finalcode, "sipUnused") ? "sipUnused" : ""));

        if (need_cpp)
        {
            prcode(fp,
"    ");
            generateClassFromVoid(cd, "sipCpp", "sipCppV", fp);
            prcode(fp, ";\n"
"\n"
                );
        }

        generateCppCodeBlock(cd->finalcode, fp);

        prcode(fp,
"}\n"
            );
    }

    /* The mixin initialisation function. */
    if (isMixin(cd))
    {
        prcode(fp,
"\n"
"\n"
            );

        if (!generating_c)
            prcode(fp,
"extern \"C\" {static int mixin_%C(PyObject *, PyObject *, PyObject *);}\n"
                , classFQCName(cd));

        prcode(fp,
"static int mixin_%C(PyObject *sipSelf, PyObject *sipArgs, PyObject *sipKwds)\n"
"{\n"
"    return sipInitMixin(sipSelf, sipArgs, sipKwds, (sipClassTypeDef *)&"
            , classFQCName(cd));

        generateTypeDefName(cd->iff, fp);

        prcode(fp, ");\n"
"}\n"
            );
    }

    /* The array allocation helpers. */
    if (generating_c || arrayHelper(cd))
    {
        prcode(fp,
"\n"
"\n"
            );

        if (!generating_c)
            prcode(fp,
"extern \"C\" {static void *array_%L(Py_ssize_t);}\n"
                , cd->iff);

        prcode(fp,
"static void *array_%L(Py_ssize_t sipNrElem)\n"
"{\n"
            , cd->iff);

        if (generating_c)
            prcode(fp,
"    return sipMalloc(sizeof (%U) * sipNrElem);\n"
                , cd);
        else
            prcode(fp,
"    return new %U[sipNrElem];\n"
                , cd);

        prcode(fp,
"}\n"
            );

        if (abiSupportsArray())
        {
            prcode(fp,
"\n"
"\n"
                );

            if (!generating_c)
                prcode(fp,
"extern \"C\" {static void array_delete_%L(void *);}\n"
                    , cd->iff);

            prcode(fp,
"static void array_delete_%L(void *sipCpp)\n"
"{\n"
                , cd->iff);

            if (generating_c)
                prcode(fp,
"    sipFree(sipCpp);\n"
                    );
            else
                prcode(fp,
"    delete[] reinterpret_cast<%U *>(sipCpp);\n"
                    , cd);

            prcode(fp,
"}\n"
                );
        }
    }

    /* The copy and assignment helpers. */
    if (generating_c || copyHelper(cd))
    {
        /*
         * The assignment helper.  We assume that there will be a valid
         * assigment operator if there is a a copy ctor.  Note that the source
         * pointer is not const.  This is to allow the source instance to be
         * modified as a consequence of the assignment, eg. if it is
         * implementing some sort of reference counting scheme.
         */
        prcode(fp,
"\n"
"\n"
            );

        if (!generating_c)
            prcode(fp,
"extern \"C\" {static void assign_%L(void *, Py_ssize_t, void *);}\n"
                , cd->iff);

        prcode(fp,
"static void assign_%L(void *sipDst, Py_ssize_t sipDstIdx, void *sipSrc)\n"
"{\n"
            , cd->iff);

        if (generating_c)
            prcode(fp,
"    ((%U *)sipDst)[sipDstIdx] = *((%U *)sipSrc);\n"
                , cd, cd);
        else
            prcode(fp,
"    reinterpret_cast<%U *>(sipDst)[sipDstIdx] = *reinterpret_cast<%U *>(sipSrc);\n"
                , cd, cd);

        prcode(fp,
"}\n"
            );

        /* The copy helper. */
        prcode(fp,
"\n"
"\n"
            );

        if (!generating_c)
            prcode(fp,
"extern \"C\" {static void *copy_%L(const void *, Py_ssize_t);}\n"
                , cd->iff);

        prcode(fp,
"static void *copy_%L(const void *sipSrc, Py_ssize_t sipSrcIdx)\n"
"{\n"
            , cd->iff);

        if (generating_c)
            prcode(fp,
"    %U *sipPtr = sipMalloc(sizeof (%U));\n"
"    *sipPtr = ((const %U *)sipSrc)[sipSrcIdx];\n"
"\n"
"    return sipPtr;\n"
                , cd, cd
                , cd);
        else
            prcode(fp,
"    return new %U(reinterpret_cast<const %U *>(sipSrc)[sipSrcIdx]);\n"
                , cd, cd);

        prcode(fp,
"}\n"
            );
    }

    /* The dealloc function. */
    if (needDealloc(cd))
    {
        prcode(fp,
"\n"
"\n"
            );

        if (!generating_c)
            prcode(fp,
"extern \"C\" {static void dealloc_%L(sipSimpleWrapper *);}\n"
                , cd->iff);

        prcode(fp,
"static void dealloc_%L(sipSimpleWrapper *sipSelf)\n"
"{\n"
            , cd->iff);

        if (tracing)
            prcode(fp,
"    sipTrace(SIP_TRACE_DEALLOCS, \"dealloc_%L()\\n\");\n"
"\n"
                , cd->iff);

        /* Disable the virtual handlers. */
        if (hasShadow(cd))
            prcode(fp,
"    if (sipIsDerivedClass(sipSelf))\n"
"        reinterpret_cast<sip%C *>(sipGetAddress(sipSelf))->sipPySelf = SIP_NULLPTR;\n"
"\n"
                ,classFQCName(cd));

        if (generating_c || isPublicDtor(cd) || (hasShadow(cd) && isProtectedDtor(cd)))
        {
            prcode(fp,
"    if (sipIsOwnedByPython(sipSelf))\n"
"    {\n"
                );

            if (isDelayedDtor(cd))
            {
                prcode(fp,
"        sipAddDelayedDtor(sipSelf);\n"
                    );
            }
            else if (generating_c)
            {
                if (cd->dealloccode != NULL)
                    generateCppCodeBlock(cd->dealloccode, fp);

                prcode(fp,
"        sipFree(sipGetAddress(sipSelf));\n"
                    );
            }
            else
            {
                prcode(fp,
"        release_%L(sipGetAddress(sipSelf), %s);\n"
                    , cd->iff, (hasShadow(cd) ? "sipIsDerivedClass(sipSelf)" : "0"));
            }

            prcode(fp,
"    }\n"
                );
        }

        prcode(fp,
"}\n"
            );
    }

    /* The type initialisation function. */
    if (canCreate(cd))
        if (generateTypeInit(cd, mod, fp) < 0)
            return -1;

    return 0;
}


/*
 * Generate the shadow (derived) class code.
 */
static int generateShadowCode(sipSpec *pt, moduleDef *mod, classDef *cd,
        FILE *fp)
{
    int nrVirts, virtNr;
    virtOverDef *vod;
    ctorDef *ct;

    nrVirts = countVirtuals(cd);

    /* Generate the wrapper class constructors. */

    for (ct = cd->ctors; ct != NULL; ct = ct->next)
    {
        ctorDef *dct;

        if (isPrivateCtor(ct))
            continue;

        if (ct->cppsig == NULL)
            continue;

        /* Check we haven't already handled this C++ signature. */
        for (dct = cd->ctors; dct != ct; dct = dct->next)
            if (dct->cppsig != NULL && sameSignature(dct->cppsig, ct->cppsig, TRUE))
                break;

        if (dct != ct)
            continue;

        prcode(fp,
"\n"
"sip%C::sip%C(",classFQCName(cd),classFQCName(cd));

        generateCalledArgs(mod, cd->iff, ct->cppsig, Definition, fp);

        prcode(fp, ")%X: %U(", ct->exceptions, cd);

        generateProtectedCallArgs(mod, ct->cppsig, fp);

        prcode(fp,"), sipPySelf(SIP_NULLPTR)\n"
"{\n"
            );

        if (tracing)
        {
            prcode(fp,
"    sipTrace(SIP_TRACE_CTORS, \"sip%C::sip%C(",classFQCName(cd),classFQCName(cd));
            generateCalledArgs(NULL, cd->iff, ct->cppsig, Declaration, fp);
            prcode(fp,")%X (this=0x%%08x)\\n\", this);\n"
"\n"
                ,ct->exceptions);
        }

        if (nrVirts > 0)
            prcode(fp,
"    memset(sipPyMethods, 0, sizeof (sipPyMethods));\n"
                );

        prcode(fp,
"}\n"
            );
    }

    /* The destructor. */

    if (!isPrivateDtor(cd))
    {
        prcode(fp,
"\n"
"sip%C::~sip%C()%X\n"
"{\n"
            ,classFQCName(cd),classFQCName(cd),cd->dtorexceptions);

        if (tracing)
            prcode(fp,
"    sipTrace(SIP_TRACE_DTORS, \"sip%C::~sip%C()%X (this=0x%%08x)\\n\", this);\n"
"\n"
                ,classFQCName(cd),classFQCName(cd),cd->dtorexceptions);

        if (cd->dtorcode != NULL)
            generateCppCodeBlock(cd->dtorcode,fp);

        prcode(fp,
"    sipInstanceDestroyedEx(&sipPySelf);\n"
"}\n"
            );
    }

    /* The meta methods if required. */
    if ((pluginPyQt5(pt) || pluginPyQt6(pt)) && isQObjectSubClass(cd))
    {
        if (!noPyQtQMetaObject(cd))
        {
            prcode(fp,
"\n"
"const QMetaObject *sip%C::metaObject() const\n"
"{\n"
"    if (sipGetInterpreter())\n"
"        return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : sip_%s_qt_metaobject(sipPySelf, sipType_%C);\n"
"\n"
"    return %S::metaObject();\n"
"}\n"
                , classFQCName(cd)
                , mod->name, classFQCName(cd)
                , classFQCName(cd));
        }

        prcode(fp,
"\n"
"int sip%C::qt_metacall(QMetaObject::Call _c, int _id, void **_a)\n"
"{\n"
"    _id = %S::qt_metacall(_c, _id, _a);\n"
"\n"
"    if (_id >= 0)\n"
"    {\n"
"        SIP_BLOCK_THREADS\n"
"        _id = sip_%s_qt_metacall(sipPySelf, sipType_%C, _c, _id, _a);\n"
"        SIP_UNBLOCK_THREADS\n"
"    }\n"
"\n"
"    return _id;\n"
"}\n"
"\n"
"void *sip%C::qt_metacast(const char *_clname)\n"
"{\n"
"    void *sipCpp;\n"
"\n"
"    return (sip_%s_qt_metacast(sipPySelf, sipType_%C, _clname, &sipCpp) ? sipCpp : %S::qt_metacast(_clname));\n"
"}\n"
            , classFQCName(cd)
            , classFQCName(cd)
            , mod->name, classFQCName(cd)
            , classFQCName(cd)
            , mod->name, classFQCName(cd), classFQCName(cd));
    }

    /* Generate the virtual catchers. */
 
    virtNr = 0;
 
    for (vod = cd->vmembers; vod != NULL; vod = vod->next)
    {
        overDef *od = vod->od;
        virtOverDef *dvod;

        if (isPrivate(od))
            continue;

        /*
         * Check we haven't already handled this C++ signature.  The same C++
         * signature should only appear more than once for overloads that are
         * enabled for different APIs and that differ in their /In/ and/or
         * /Out/ annotations.
         */
        for (dvod = cd->vmembers; dvod != vod; dvod = dvod->next)
            if (strcmp(dvod->od->cppname, od->cppname) == 0 && sameSignature(dvod->od->cppsig, od->cppsig, TRUE))
                break;

        if (dvod == vod)
            if (generateVirtualCatcher(mod, cd, virtNr++, vod, fp) < 0)
                return -1;
    }

    /* Generate the wrapper around each protected member function. */
    generateProtectedDefinitions(mod, cd, fp);

    return 0;
}


/*
 * Generate the protected enums for a class.
 */
static void generateProtectedEnums(sipSpec *pt,classDef *cd,FILE *fp)
{
    enumDef *ed;

    for (ed = pt->enums; ed != NULL; ed = ed->next)
    {
        char *eol;
        mroDef *mro;
        enumMemberDef *emd;

        if (!isProtectedEnum(ed))
            continue;

        /* See if the class defining the enum is in our class hierachy. */
        for (mro = cd->mro; mro != NULL; mro = mro->next)
            if (mro->cd == ed->ecd)
                break;

        if (mro == NULL)
            continue;

        prcode(fp,
"\n"
"    /* Expose this protected enum. */\n"
"    enum");

        if (ed->fqcname != NULL)
            prcode(fp," sip%s",scopedNameTail(ed->fqcname));

        prcode(fp," {");

        eol = "\n";

        for (emd = ed->members; emd != NULL; emd = emd->next)
        {
            prcode(fp,"%s"
"        %s = %S::%s",eol,emd->cname,classFQCName(ed->ecd),emd->cname);

            eol = ",\n";
        }

        prcode(fp,"\n"
"    };\n"
            );
    }
}


/*
 * Generate the catcher for a virtual function.
 */
static int generateVirtualCatcher(moduleDef *mod, classDef *cd, int virtNr,
        virtOverDef *vod, FILE *fp)
{
    overDef *od = vod->od;
    argDef *res = &od->cppsig->result;

    normaliseArg(res);
    normaliseArgs(od->cppsig);

    prcode(fp,
"\n");

    generateBaseType(cd->iff, res, TRUE, STRIP_NONE, fp);

    prcode(fp," sip%C::%O(",classFQCName(cd),od);
    generateCalledArgs(mod, cd->iff, od->cppsig, Definition, fp);
    prcode(fp,")%s%X\n"
"{\n"
        ,(isConst(od) ? " const" : ""),od->exceptions);

    if (tracing)
    {
        prcode(fp,
"    sipTrace(SIP_TRACE_CATCHERS, \"");

        generateBaseType(cd->iff, res, TRUE, STRIP_GLOBAL, fp);
        prcode(fp," sip%C::%O(",classFQCName(cd),od);
        generateCalledArgs(NULL, cd->iff, od->cppsig, Declaration, fp);
        prcode(fp,")%s%X (this=0x%%08x)\\n\", this);\n"
"\n"
            ,(isConst(od) ? " const" : ""),od->exceptions);
    }

    restoreArgs(od->cppsig);
    restoreArg(res);

    prcode(fp,
"    sip_gilstate_t sipGILState;\n"
"    PyObject *sipMeth;\n"
"\n"
        );

    if (abiVersion >= ABI_12_8)
    {
        /* For ABI v12.8 and later. */
        prcode(fp,
"    sipMeth = sipIsPyMethod(&sipGILState, ");

        if (isConst(od))
            prcode(fp, "const_cast<char *>(&sipPyMethods[%d]), const_cast<sipSimpleWrapper **>(&sipPySelf), ", virtNr);
        else
            prcode(fp, "&sipPyMethods[%d], &sipPySelf, ", virtNr);

        if (isAbstract(od))
            prcode(fp, "%N", cd->pyname);
        else
            prcode(fp, "SIP_NULLPTR");

        prcode(fp, ", %N);\n"
            , od->common->pyname);
    }
    else
    {
        /* For ABI v12.7 and earlier. */
        prcode(fp,
"    sipMeth = sipIsPyMethod(&sipGILState, ");

        if (isConst(od))
            prcode(fp, "const_cast<char *>(");

        prcode(fp, "&sipPyMethods[%d]", virtNr);

        if (isConst(od))
            prcode(fp, ")");

        prcode(fp,", sipPySelf, ");

        if (isAbstract(od))
            prcode(fp, "%N", cd->pyname);
        else
            prcode(fp, "SIP_NULLPTR");

        prcode(fp, ", %N);\n"
            , od->common->pyname);
    }

    /* The rest of the common code. */

    if (res->atype == void_type && res->nrderefs == 0)
        res = NULL;

    prcode(fp,
"\n"
"    if (!sipMeth)\n"
        );

    if (od->virtcallcode != NULL)
    {
        prcode(fp,
"    {\n");

        if (res != NULL)
        {
            prcode(fp,
"        ");

            generateNamedBaseType(cd->iff, res, "sipRes", TRUE, STRIP_NONE,
                    fp);

            prcode(fp, ";\n"
                );
        }

        prcode(fp,
"\n"
            );

        generateCppCodeBlock(od->virtcallcode, fp);

        prcode(fp,
"\n"
"        return%s;\n"
"    }\n"
            , (res != NULL ? " sipRes" : ""));
    }
    else if (isAbstract(od))
    {
        if (generateDefaultInstanceReturn(res, "    ", fp) < 0)
            return -1;
    }
    else
    {
        int a;

        if (res == NULL)
            prcode(fp,
"    {\n"
"        ");
        else
            prcode(fp,
"        return ");

        prcode(fp, "%S::%O(", classFQCName(cd), od);
 
        for (a = 0; a < od->cppsig->nrArgs; ++a)
        {
            argDef *ad = &od->cppsig->args[a];

            prcode(fp, "%s%a", (a == 0 ? "" : ", "), mod, ad, a);
        }
 
        prcode(fp,");\n"
            );

        if (res == NULL)
        {
            /*
             * Note that we should also generate this if the function returns a
             * value, but we are lazy and this is all that is needed by PyQt.
             */
            if (isNewThread(od))
                prcode(fp,
"        sipEndThread();\n"
                    );

            prcode(fp,
"        return;\n"
"    }\n"
                );
        }
    }

    prcode(fp,
"\n"
        );

    generateVirtHandlerCall(mod, cd, vod, res, "    ", fp);

    prcode(fp,
"}\n"
        );

    return 0;
}


/*
 * Generate a call to a single virtual handler.
 */
static void generateVirtHandlerCall(moduleDef *mod, classDef *cd,
        virtOverDef *vod, argDef *res, const char *indent, FILE *fp)
{
    overDef *od = vod->od;
    virtHandlerDef *vhd = vod->virthandler;
    virtErrorHandler *veh;
    signatureDef saved;
    argDef *ad;
    int a, args_keep = FALSE, result_keep = FALSE;
    const char *trailing = "";

    saved = *vhd->cppsig;
    fakeProtectedArgs(vhd->cppsig);

    prcode(fp,
"%sextern ", indent);

    generateBaseType(cd->iff, &od->cppsig->result, TRUE, STRIP_NONE, fp);

    prcode(fp, " sipVH_%s_%d(sip_gilstate_t, sipVirtErrorHandlerFunc, sipSimpleWrapper *, PyObject *", mod->name, vhd->virthandlernr);

    if (vhd->cppsig->nrArgs > 0)
    {
        prcode(fp, ", ");
        generateCalledArgs(NULL, cd->iff, vhd->cppsig, Declaration, fp);
    }

    *vhd->cppsig = saved;

    /* Add extra arguments for all the references we need to keep. */
    if (res != NULL && keepPyReference(res))
    {
        result_keep = TRUE;
        res->key = mod->next_key--;
        prcode(fp, ", int");
    }

    for (ad = od->cppsig->args, a = 0; a < od->cppsig->nrArgs; ++a, ++ad)
        if (isOutArg(ad) && keepPyReference(ad))
        {
            args_keep = TRUE;
            ad->key = mod->next_key--;
            prcode(fp, ", int");
        }

    prcode(fp,");\n"
        );

    prcode(fp,
"\n"
"%s", indent);

    if (!isNewThread(od) && res != NULL)
    {
        prcode(fp, "return ");

        if (res->atype == enum_type && isProtectedEnum(res->u.ed))
        {
            normaliseArg(res);
            prcode(fp, "static_cast<%E>(", res->u.ed);
            trailing = ")";
            restoreArg(res);
        }
    }

    prcode(fp, "sipVH_%s_%d(sipGILState, ", mod->name, vhd->virthandlernr);

    veh = vhd->veh;

    if (veh == NULL)
        prcode(fp, "0");
    else if (veh->mod == mod)
        prcode(fp, "sipVEH_%s_%s" , mod->name, veh->name);
    else
        prcode(fp, "sipImportedVirtErrorHandlers_%s_%s[%d].iveh_handler", mod->name, veh->mod->name, veh->index);

    prcode(fp, ", sipPySelf, sipMeth");

    for (ad = od->cppsig->args, a = 0; a < od->cppsig->nrArgs; ++a, ++ad)
    {
        if (ad->atype == class_type && isProtectedClass(ad->u.cd))
            prcode(fp, ", %s%a", ((isReference(ad) || ad->nrderefs == 0) ? "&" : ""), mod, ad, a);
        else if (ad->atype == enum_type && isProtectedEnum(ad->u.ed))
            prcode(fp, ", (%E)%a", ad->u.ed, mod, ad, a);
        else
            prcode(fp, ", %a", mod, ad, a);
    }

    /* Pass the keys to maintain the kept references. */
    if (result_keep)
        prcode(fp, ", %d", res->key);

    if (args_keep)
        for (ad = od->cppsig->args, a = 0; a < od->cppsig->nrArgs; ++a, ++ad)
            if (isOutArg(ad) && keepPyReference(ad))
                prcode(fp, ", %d", ad->key);

    prcode(fp,")%s;\n"
        , trailing);

    if (isNewThread(od))
        prcode(fp,
"\n"
"%ssipEndThread();\n"
            , indent);
}


/*
 * Generate a cast to zero.
 */
static void generateCastZero(argDef *ad, FILE *fp)
{
    switch (ad->atype)
    {
    case enum_type:
        {
            enumDef *ed = ad->u.ed;

            if (ed->members != NULL)
            {
                if (isScopedEnum(ed))
                    prcode(fp, "%E", ed);
                else if (ed->ecd != NULL)
                    prEnumMemberScope(ed->members, fp);

                prcode(fp, "::%s", ed->members->cname);

                return;
            }

            prcode(fp, "(%E)0", ed);
        }

    case pyobject_type:
    case pytuple_type:
    case pylist_type:
    case pydict_type:
    case pycallable_type:
    case pyslice_type:
    case pytype_type:
    case pybuffer_type:
    case pyenum_type:
    case ellipsis_type:
        prcode(fp, "SIP_NULLPTR");
        break;

    default:
        prcode(fp, "0");
    }
}


/*
 * Generate a statement to return the default instance of a type typically on
 * error (ie. when there is nothing sensible to return).
 */
static int generateDefaultInstanceReturn(argDef *res, const char *indent,
        FILE *fp)
{
    codeBlockList *instance_code;

    /* Handle the trivial case. */
    if (res == NULL)
    {
        prcode(fp,
"%s    return;\n"
            , indent);

        return 0;
    }

    /* Handle any %InstanceCode. */
    instance_code = NULL;

    if (res->nrderefs == 0)
    {
        if (res->atype == mapped_type)
            instance_code = res->u.mtd->instancecode;
        else if (res->atype == class_type)
            instance_code = res->u.cd->instancecode;
    }

    if (instance_code != NULL)
    {
        argDef res_noconstref;

        res_noconstref = *res;
        resetIsConstArg(&res_noconstref);
        resetIsReference(&res_noconstref);

        prcode(fp,
"%s{\n"
"%s    static %B *sipCpp = SIP_NULLPTR;\n"
"\n"
"%s    if (!sipCpp)\n"
"%s    {\n"
            , indent
            , indent, &res_noconstref
            , indent
            , indent);

        generateCppCodeBlock(instance_code, fp);

        prcode(fp,
"%s    }\n"
"\n"
"%s    return *sipCpp;\n"
"%s}\n"
            , indent
            , indent
            , indent);

        return 0;
    }

    prcode(fp,
"%s    return ", indent);

    if (res->atype == mapped_type && res->nrderefs == 0)
    {
        argDef res_noconstref;

        /*
         * We don't know anything about the mapped type so we just hope is has
         * a default ctor.
         */

        if (isReference(res))
            prcode(fp,"*new ");

        res_noconstref = *res;
        resetIsConstArg(&res_noconstref);
        resetIsReference(&res_noconstref);
        prcode(fp,"%B()",&res_noconstref);
    }
    else if (res->atype == class_type && res->nrderefs == 0)
    {
        ctorDef *ct = res->u.cd->defctor;

        /*
         * If we don't have a suitable ctor then the generated code will issue
         * an error message.
         */
        if (ct != NULL && isPublicCtor(ct) && ct->cppsig != NULL)
        {
            argDef res_noconstref;

            /*
             * If this is a badly designed class.  We can only generate correct
             * code by leaking memory.
             */
            if (isReference(res))
                prcode(fp,"*new ");

            res_noconstref = *res;
            resetIsConstArg(&res_noconstref);
            resetIsReference(&res_noconstref);
            prcode(fp,"%B",&res_noconstref);

            generateCallDefaultCtor(ct,fp);
        }
        else
        {
            errorScopedName(classFQCName(res->u.cd));
            return error(" must have a default constructor\n");
        }
    }
    else
        generateCastZero(res,fp);

    prcode(fp,";\n"
        );

    return 0;
}


/*
 * Generate the call to a default ctor.
 */
static void generateCallDefaultCtor(ctorDef *ct, FILE *fp)
{
    int a;

    prcode(fp, "(");

    for (a = 0; a < ct->cppsig->nrArgs; ++a)
    {
        argDef *ad = &ct->cppsig->args[a];
        argType atype = ad->atype;

        if (ad->defval != NULL)
            break;

        if (a > 0)
            prcode(fp, ", ");

        /* Do what we can to provide type information to the compiler. */
        if (atype == class_type && ad->nrderefs > 0 && !isReference(ad))
            prcode(fp, "static_cast<%B>(0)", ad);
        else if (atype == enum_type)
            prcode(fp, "static_cast<%E>(0)", ad->u.ed);
        else if (atype == float_type || atype == cfloat_type)
            prcode(fp, "0.0F");
        else if (atype == double_type || atype == cdouble_type)
            prcode(fp, "0.0");
        else if (atype == uint_type || atype == size_type)
            prcode(fp, "0U");
        else if (atype == long_type || atype == longlong_type)
            prcode(fp, "0L");
        else if (atype == ulong_type || atype == ulonglong_type)
            prcode(fp, "0UL");
        else if ((atype == ascii_string_type || atype == latin1_string_type || atype == utf8_string_type || atype == ustring_type || atype == sstring_type || atype == string_type) && ad->nrderefs == 0)
            prcode(fp, "'\\0'");
        else if (atype == wstring_type && ad->nrderefs == 0)
            prcode(fp, "L'\\0'");
        else
            prcode(fp, "0");
    }

    prcode(fp, ")");
}


/*
 * Generate the declarations of the protected wrapper functions for a class.
 */
static void generateProtectedDeclarations(classDef *cd,FILE *fp)
{
    int noIntro;
    visibleList *vl;

    noIntro = TRUE;

    for (vl = cd->visible; vl != NULL; vl = vl->next)
    {
        overDef *od;

        if (vl->m->slot != no_slot)
            continue;

        for (od = vl->cd->overs; od != NULL; od = od->next)
        {
            if (od->common != vl->m || !isProtected(od))
                continue;

            /*
             * Check we haven't already handled this signature (eg. if we have
             * specified the same method with different Python names.
             */
            if (isDuplicateProtected(cd, od))
                continue;

            if (noIntro)
            {
                prcode(fp,
"\n"
"    /*\n"
"     * There is a public method for every protected method visible from\n"
"     * this class.\n"
"     */\n"
                    );

                noIntro = FALSE;
            }

            prcode(fp,
"    ");

            if (isStatic(od))
                prcode(fp,"static ");

            generateBaseType(cd->iff, &od->cppsig->result, TRUE, STRIP_NONE,
                    fp);

            if (!isStatic(od) && !isAbstract(od) && (isVirtual(od) || isVirtualReimp(od)))
            {
                prcode(fp, " sipProtectVirt_%s(bool", od->cppname);

                if (od->cppsig->nrArgs > 0)
                    prcode(fp, ", ");
            }
            else
                prcode(fp, " sipProtect_%s(", od->cppname);

            generateCalledArgs(NULL, cd->iff, od->cppsig, Declaration, fp);
            prcode(fp,")%s;\n"
                ,(isConst(od) ? " const" : ""));
        }
    }
}


/*
 * Generate the definitions of the protected wrapper functions for a class.
 */
static void generateProtectedDefinitions(moduleDef *mod, classDef *cd, FILE *fp)
{
    visibleList *vl;

    for (vl = cd->visible; vl != NULL; vl = vl->next)
    {
        overDef *od;

        if (vl->m->slot != no_slot)
            continue;

        for (od = vl->cd->overs; od != NULL; od = od->next)
        {
            const char *mname = od->cppname;
            int parens;
            argDef *res;

            if (od->common != vl->m || !isProtected(od))
                continue;

            /*
             * Check we haven't already handled this signature (eg. if we have
             * specified the same method with different Python names.
             */
            if (isDuplicateProtected(cd, od))
                continue;

            prcode(fp,
"\n"
                );

            generateBaseType(cd->iff, &od->cppsig->result, TRUE, STRIP_NONE,
                    fp);

            if (!isStatic(od) && !isAbstract(od) && (isVirtual(od) || isVirtualReimp(od)))
            {
                prcode(fp, " sip%C::sipProtectVirt_%s(bool sipSelfWasArg", classFQCName(cd), mname);

                if (od->cppsig->nrArgs > 0)
                    prcode(fp, ", ");
            }
            else
                prcode(fp, " sip%C::sipProtect_%s(", classFQCName(cd), mname);

            generateCalledArgs(mod, cd->iff, od->cppsig, Definition, fp);
            prcode(fp,")%s\n"
"{\n"
                ,(isConst(od) ? " const" : ""));

            parens = 1;

            res = &od->cppsig->result;

            if (res->atype == void_type && res->nrderefs == 0)
                prcode(fp,
"    ");
            else
            {
                prcode(fp,
"    return ");

                if (res->atype == class_type && isProtectedClass(res->u.cd))
                {
                    prcode(fp,"static_cast<%U *>(",res->u.cd);
                    ++parens;
                }
                else if (res->atype == enum_type && isProtectedEnum(res->u.ed))
                {
                    /*
                     * One or two older compilers can't handle a static_cast
                     * here so we revert to a C-style cast.
                     */
                    prcode(fp,"(%E)",res->u.ed);
                }
            }

            if (!isAbstract(od))
            {
                if (isVirtual(od) || isVirtualReimp(od))
                {
                    prcode(fp, "(sipSelfWasArg ? %U::%s(", vl->cd, mname);

                    generateProtectedCallArgs(mod, od->cppsig, fp);

                    prcode(fp, ") : ");
                    ++parens;
                }
                else
                {
                    prcode(fp, "%U::", vl->cd);
                }
            }

            prcode(fp,"%s(",mname);

            generateProtectedCallArgs(mod, od->cppsig, fp);

            while (parens--)
                prcode(fp,")");

            prcode(fp,";\n"
"}\n"
                );
        }
    }
}


/*
 * Return TRUE if a protected method is a duplicate.
 */
static int isDuplicateProtected(classDef *cd, overDef *target)
{
    visibleList *vl;

    for (vl = cd->visible; vl != NULL; vl = vl->next)
    {
        overDef *od;

        if (vl->m->slot != no_slot)
            continue;

        for (od = vl->cd->overs; od != NULL; od = od->next)
        {
            if (od->common != vl->m || !isProtected(od))
                continue;

            if (od == target)
                return FALSE;

            if (strcmp(od->cppname, target->cppname) == 0 && sameSignature(od->cppsig, target->cppsig, TRUE))
                return TRUE;
        }
    }

    /* We should never actually get here. */
    return FALSE;
}


/*
 * Generate the arguments for a call to a protected method.
 */
static void generateProtectedCallArgs(moduleDef *mod, signatureDef *sd,
        FILE *fp)
{
    int a;

    for (a = 0; a < sd->nrArgs; ++a)
    {
        argDef *ad = &sd->args[a];

        if (a > 0)
            prcode(fp, ", ");

        if (ad->atype == enum_type && isProtectedEnum(ad->u.ed))
            prcode(fp, "(%S)", ad->u.ed->fqcname);

        prcode(fp, "%a", mod, ad, a);
    }
}


/*
 * Generate the function that does most of the work to handle a particular
 * virtual function.
 */
static int generateVirtualHandler(moduleDef *mod, virtHandlerDef *vhd,
        FILE *fp)
{
    int a, nrvals, res_isref;
    argDef *res, res_noconstref, *ad;
    signatureDef saved;
    codeBlockList *res_instancecode;

    res = &vhd->cppsig->result;

    res_isref = FALSE;
    res_instancecode = NULL;

    if (res->atype == void_type && res->nrderefs == 0)
    {
        res = NULL;
    }
    else
    {
        /*
         * If we are returning a reference to an instance then we take care to
         * handle Python errors but still return a valid C++ instance.
         */
        if ((res->atype == class_type || res->atype == mapped_type) && res->nrderefs == 0)
        {
            if (isReference(res))
                res_isref = TRUE;
            else if (res->atype == class_type)
                res_instancecode = res->u.cd->instancecode;
            else
                res_instancecode = res->u.mtd->instancecode;
        }

        res_noconstref = *res;
        resetIsConstArg(&res_noconstref);
        resetIsReference(&res_noconstref);
    }

    prcode(fp,
"\n"
        );

    saved = *vhd->cppsig;
    fakeProtectedArgs(vhd->cppsig);

    generateBaseType(NULL, &vhd->cppsig->result, TRUE, STRIP_NONE, fp);

    prcode(fp," sipVH_%s_%d(sip_gilstate_t sipGILState, sipVirtErrorHandlerFunc sipErrorHandler, sipSimpleWrapper *sipPySelf, PyObject *sipMethod"
        , mod->name, vhd->virthandlernr);

    if (vhd->cppsig->nrArgs > 0)
    {
        prcode(fp,", ");
        generateCalledArgs(mod, NULL, vhd->cppsig, Definition, fp);
    }

    *vhd->cppsig = saved;

    /* Declare the extra arguments for kept references. */
    if (res != NULL && keepPyReference(res))
    {
        prcode(fp, ", int");

        if (vhd->virtcode == NULL || usedInCode(vhd->virtcode, "sipResKey"))
            prcode(fp, " sipResKey");
    }

    for (ad = vhd->cppsig->args, a = 0; a < vhd->cppsig->nrArgs; ++a, ++ad)
        if (isOutArg(ad) && keepPyReference(ad))
            prcode(fp, ", int %aKey", mod, ad, a);

    prcode(fp,")\n"
"{\n"
        );

    if (res != NULL)
    {
        if (res_instancecode != NULL)
        {
            prcode(fp,
"    static %B *sipCpp = SIP_NULLPTR;\n"
"\n"
"    if (!sipCpp)\n"
"    {\n"
                , &res_noconstref);

            generateCppCodeBlock(res_instancecode, fp);

            prcode(fp,
"    }\n"
"\n"
                );
        }

        prcode(fp, "    ");

        /*
         * wchar_t * return values are always on the heap.  To reduce memory
         * leaks we keep the last result around until we have a new one.  This
         * means that ownership of the return value stays with the function
         * returning it - which is consistent with how other types work, even
         * thought it may not be what's required in all cases.  Note that we
         * should do this in the code that calls the handler instead of here
         * (as we do with strings) so that it doesn't get shared between all
         * callers.
         */
        if (res->atype == wstring_type && res->nrderefs == 1)
            prcode(fp, "static ");

        generateBaseType(NULL, &res_noconstref, TRUE, STRIP_NONE, fp);

        prcode(fp," %ssipRes",(res_isref ? "*" : ""));

        if ((res->atype == class_type || res->atype == mapped_type || res->atype == template_type) && res->nrderefs == 0)
        {
            if (res_instancecode != NULL)
            {
                prcode(fp, " = *sipCpp");
            }
            else if (res->atype == class_type)
            {
                ctorDef *ct = res->u.cd->defctor;

                if (ct != NULL && isPublicCtor(ct) && ct->cppsig != NULL && ct->cppsig->nrArgs > 0 && ct->cppsig->args[0].defval == NULL)
                    generateCallDefaultCtor(ct,fp);
            }
        }
        else if (res->atype == enum_type && isProtectedEnum(res->u.ed))
        {
            /*
             * Currently SIP generates the virtual handlers before any shadow
             * classes which means that the compiler doesn't know about the
             * handling of protected enums.  Therefore we can only initialise
             * to 0.
             */
            prcode(fp," = 0");
        }
        else
        {
            /*
             * We initialise the result to try and suppress a compiler warning.
             */
            prcode(fp," = ");
            generateCastZero(res,fp);
        }

        prcode(fp,";\n"
            );

        if (res->atype == wstring_type && res->nrderefs == 1)
            prcode(fp,
"\n"
"    if (sipRes)\n"
"    {\n"
"        // Return any previous result to the heap.\n"
"        sipFree(%s);\n"
"        sipRes = SIP_NULLPTR;\n"
"    }\n"
"\n"
                , (isConstArg(res) ? "const_cast<wchar_t *>(sipRes)" : "sipRes"));
    }

    if (vhd->virtcode != NULL)
    {
        int error_flag = needErrorFlag(vhd->virtcode);
        int old_error_flag = needOldErrorFlag(vhd->virtcode);

        if (error_flag)
            prcode(fp,
"    sipErrorState sipError = sipErrorNone;\n"
                );
        else if (old_error_flag)
            prcode(fp,
"    int sipIsErr = 0;\n"
                );

        prcode(fp,
"\n"
            );

        generateCppCodeBlock(vhd->virtcode,fp);

        prcode(fp,
"\n"
"    Py_DECREF(sipMethod);\n"
            );

        if (error_flag || old_error_flag)
            prcode(fp,
"\n"
"    if (%s)\n"
"        sipCallErrorHandler(sipErrorHandler, sipPySelf, sipGILState);\n"
                , (error_flag ? "sipError != sipErrorNone" : "sipIsErr"));

        prcode(fp,
"\n"
"    SIP_RELEASE_GIL(sipGILState)\n"
            );

        if (res != NULL)
            prcode(fp,
"\n"
"    return sipRes;\n"
                );

        prcode(fp,
"}\n"
            );

        return 0;
    }

    /* See how many values we expect. */
    nrvals = (res != NULL ? 1 : 0);

    for (a = 0; a < vhd->pysig->nrArgs; ++a)
        if (isOutArg(&vhd->pysig->args[a]))
            ++nrvals;

    /* Call the method. */
    if (nrvals == 0)
        prcode(fp,
"    sipCallProcedureMethod(sipGILState, sipErrorHandler, sipPySelf, sipMethod, ");
    else
        prcode(fp,
"    PyObject *sipResObj = sipCallMethod(SIP_NULLPTR, sipMethod, ");

    generateTupleBuilder(mod, vhd->pysig, fp);

    if (nrvals == 0)
    {
        prcode(fp, ");\n"
"}\n"
            );

        return 0;
    }

    prcode(fp, ");\n"
"\n"
"    %ssipParseResultEx(sipGILState, sipErrorHandler, sipPySelf, sipMethod, sipResObj, \"", ((res_isref || abortOnExceptionVH(vhd)) ? "int sipRc = " : ""));

    /* Build the format string. */
    if (nrvals == 0)
        prcode(fp,"Z");
    else
    {
        if (nrvals > 1)
            prcode(fp,"(");

        if (res != NULL)
            prcode(fp, "%s", getParseResultFormat(res, res_isref, isTransferVH(vhd)));

        for (a = 0; a < vhd->pysig->nrArgs; ++a)
        {
            argDef *ad = &vhd->pysig->args[a];

            if (isOutArg(ad))
                prcode(fp, "%s", getParseResultFormat(ad, FALSE, FALSE));
        }

        if (nrvals > 1)
            prcode(fp,")");
    }

    prcode(fp,"\"");

    /* Pass the destination pointers. */
    if (res != NULL)
    {
        generateParseResultExtraArgs(NULL, res, -1, fp);
        prcode(fp, ", &sipRes");
    }

    for (a = 0; a < vhd->pysig->nrArgs; ++a)
    {
        argDef *ad = &vhd->pysig->args[a];

        if (isOutArg(ad))
        {
            generateParseResultExtraArgs(mod, ad, a, fp);
            prcode(fp, ", %s%a", (isReference(ad) ? "&" : ""), mod, ad, a);
        }
    }

    prcode(fp, ");\n"
        );

    if (res != NULL)
    {
        if (res_isref || abortOnExceptionVH(vhd))
        {
            prcode(fp,
"\n"
"    if (sipRc < 0)\n"
                );

            if (abortOnExceptionVH(vhd))
                prcode(fp,
"        abort();\n"
                    );
            else if (generateDefaultInstanceReturn(res, "    ", fp) < 0)
                return -1;
        }

        prcode(fp,
"\n"
"    return %ssipRes;\n"
            , (res_isref ? "*" : ""));
    }

    prcode(fp,
"}\n"
        );

    return 0;
}


/*
 * Generate the extra arguments needed by sipParseResultEx() for a particular
 * type.
 */
static void generateParseResultExtraArgs(moduleDef *mod, argDef *ad, int argnr,
        FILE *fp)
{
    switch (ad->atype)
    {
    case mapped_type:
        prcode(fp, ", sipType_%T", ad);
        break;

    case class_type:
        prcode(fp, ", sipType_%C", classFQCName(ad->u.cd));
        break;

    case pytuple_type:
        prcode(fp,", &PyTuple_Type");
        break;

    case pylist_type:
        prcode(fp,", &PyList_Type");
        break;

    case pydict_type:
        prcode(fp,", &PyDict_Type");
        break;

    case pyslice_type:
        prcode(fp,", &PySlice_Type");
        break;

    case pytype_type:
        prcode(fp,", &PyType_Type");
        break;

    case enum_type:
        if (ad->u.ed->fqcname != NULL)
            prcode(fp, ", sipType_%C", ad->u.ed->fqcname);
        break;

    case capsule_type:
        prcode(fp,", \"%S\"", ad->u.cap);
        break;

    default:
        if (keepPyReference(ad))
        {
            if (argnr < 0)
                prcode(fp, ", sipResKey");
            else
                prcode(fp, ", %aKey", mod, ad, argnr);
        }
    }
}


/*
 * Return the format characters used by sipParseResultEx() for a particular
 * type.
 */
static const char *getParseResultFormat(argDef *ad, int res_isref, int xfervh)
{
    switch (ad->atype)
    {
    case mapped_type:
    case fake_void_type:
    case class_type:
        {
            static const char *type_formats[] = {
                "H0", "H1", "H2", "H3", "H4", "H5", "H6", "H7"
            };

            int f = 0x00;

            if (ad->nrderefs == 0)
            {
                f |= 0x01;

                if (!res_isref)
                    f |= 0x04;
            }
            else if (ad->nrderefs == 1)
            {
                if (isOutArg(ad))
                    f |= 0x04;
                else if (isDisallowNone(ad))
                    f |= 0x01;
            }

            if (xfervh)
                f |= 0x02;

            return type_formats[f];
        }

    case bool_type:
    case cbool_type:
        return "b";

    case ascii_string_type:
        return ((ad->nrderefs == 0) ? "aA" : "AA");

    case latin1_string_type:
        return ((ad->nrderefs == 0) ? "aL" : "AL");

    case utf8_string_type:
        return ((ad->nrderefs == 0) ? "a8" : "A8");

    case sstring_type:
    case ustring_type:
    case string_type:
        return ((ad->nrderefs == 0) ? "c" : "B");

    case wstring_type:
        return ((ad->nrderefs == 0) ? "w" : "x");

    case enum_type:
        return ((ad->u.ed->fqcname != NULL) ? "F" : "e");

    case byte_type:
        /*
         * Note that this assumes that char is signed.  We should not make that
         * assumption.
         */

    case sbyte_type:
        return "L";

    case ubyte_type:
        return "M";

    case ushort_type:
        return "t";

    case short_type:
        return "h";

    case int_type:
    case cint_type:
        return "i";

    case uint_type:
        return "u";

    case size_type:
        return "=";

    case long_type:
        return "l";

    case ulong_type:
        return "m";

    case longlong_type:
        return "n";

    case ulonglong_type:
        return "o";

    case struct_type:
    case union_type:
    case void_type:
        return "V";

    case capsule_type:
        return "z";

    case float_type:
    case cfloat_type:
        return "f";

    case double_type:
    case cdouble_type:
        return "d";

    case pyobject_type:
        return "O";

    case pytuple_type:
    case pylist_type:
    case pydict_type:
    case pyslice_type:
    case pytype_type:
        return (isAllowNone(ad) ? "N" : "T");

    case pybuffer_type:
        return (isAllowNone(ad) ? "$" : "!");

    case pyenum_type:
        return (isAllowNone(ad) ? "^" : "&");

    /* Supress a compiler warning. */
    default:
        ;
    }

    /* We should never get here. */
    return " ";
}


/*
 * Generate the code to build a tuple of Python arguments.
 */
static void generateTupleBuilder(moduleDef *mod, signatureDef *sd,FILE *fp)
{
    int a, arraylenarg;

    /* Suppress a compiler warning. */
    arraylenarg = 0;

    prcode(fp,"\"");

    for (a = 0; a < sd->nrArgs; ++a)
    {
        char *fmt = "";
        argDef *ad = &sd->args[a];

        if (!isInArg(ad))
            continue;

        switch (ad->atype)
        {
        case ascii_string_type:
        case latin1_string_type:
        case utf8_string_type:
            if (ad->nrderefs == 0 || (ad->nrderefs == 1 && isOutArg(ad)))
                fmt = "a";
            else
                fmt = "A";

            break;

        case sstring_type:
        case ustring_type:
        case string_type:
            if (ad->nrderefs == 0 || (ad->nrderefs == 1 && isOutArg(ad)))
                fmt = "c";
            else if (isArray(ad))
                fmt = "g";
            else
                fmt = "s";

            break;

        case wstring_type:
            if (ad->nrderefs == 0 || (ad->nrderefs == 1 && isOutArg(ad)))
                fmt = "w";
            else if (isArray(ad))
                fmt = "G";
            else
                fmt = "x";

            break;

        case bool_type:
        case cbool_type:
            fmt = "b";
            break;

        case enum_type:
            fmt = (ad->u.ed->fqcname != NULL) ? "F" : "e";
            break;

        case cint_type:
            fmt = "i";
            break;

        case uint_type:
            if (isArraySize(ad))
                arraylenarg = a;
            else
                fmt = "u";

            break;

        case int_type:
            if (isArraySize(ad))
                arraylenarg = a;
            else
                fmt = "i";

            break;

        case size_type:
            if (isArraySize(ad))
                arraylenarg = a;
            else
                fmt = "=";

            break;

        case byte_type:
        case sbyte_type:
            if (isArraySize(ad))
                arraylenarg = a;
            else
                fmt = "L";

            break;

        case ubyte_type:
            if (isArraySize(ad))
                arraylenarg = a;
            else
                fmt = "M";

            break;

        case ushort_type:
            if (isArraySize(ad))
                arraylenarg = a;
            else
                fmt = "t";

            break;

        case short_type:
            if (isArraySize(ad))
                arraylenarg = a;
            else
                fmt = "h";

            break;

        case long_type:
            if (isArraySize(ad))
                arraylenarg = a;
            else
                fmt = "l";

            break;

        case ulong_type:
            if (isArraySize(ad))
                arraylenarg = a;
            else
                fmt = "m";

            break;

        case longlong_type:
            if (isArraySize(ad))
                arraylenarg = a;
            else
                fmt = "n";

            break;

        case ulonglong_type:
            if (isArraySize(ad))
                arraylenarg = a;
            else
                fmt = "o";

            break;

        case struct_type:
        case union_type:
        case void_type:
            fmt = "V";
            break;

        case capsule_type:
            fmt = "z";
            break;

        case float_type:
        case cfloat_type:
            fmt = "f";
            break;

        case double_type:
        case cdouble_type:
            fmt = "d";
            break;

        case mapped_type:
        case class_type:
            if (isArray(ad))
            {
                fmt = "r";
                break;
            }

            if (needsHeapCopy(ad, TRUE))
            {
                fmt = "N";
                break;
            }

            /* Drop through. */

        case fake_void_type:
            fmt = "D";
            break;

        case pyobject_type:
        case pytuple_type:
        case pylist_type:
        case pydict_type:
        case pycallable_type:
        case pyslice_type:
        case pytype_type:
        case pybuffer_type:
        case pyenum_type:
            fmt = "S";
            break;

        /* Suppress a compiler warning. */
        default:
            ;
        }

        prcode(fp,fmt);
    }

    prcode(fp,"\"");

    for (a = 0; a < sd->nrArgs; ++a)
    {
        int derefs;
        argDef *ad = &sd->args[a];

        if (!isInArg(ad))
            continue;

        derefs = ad->nrderefs;

        switch (ad->atype)
        {
        case ascii_string_type:
        case latin1_string_type:
        case utf8_string_type:
        case sstring_type:
        case ustring_type:
        case string_type:
        case wstring_type:
            if (!(ad->nrderefs == 0 || (ad->nrderefs == 1 && isOutArg(ad))))
                --derefs;

            break;

        case mapped_type:
        case fake_void_type:
        case class_type:
            if (ad->nrderefs > 0)
                --derefs;

            break;

        case struct_type:
        case union_type:
        case void_type:
            --derefs;
            break;

        /* Supress a compiler warning. */
        default:
            ;
        }

        if (ad->atype == mapped_type || ad->atype == class_type ||
            ad->atype == fake_void_type)
        {
            int copy = needsHeapCopy(ad, TRUE);

            prcode(fp,", ");

            if (copy)
            {
                prcode(fp,"new %b(",ad);
            }
            else
            {
                if (isConstArg(ad))
                    prcode(fp, "const_cast<%D *>(", ad);

                if (ad->nrderefs == 0)
                    prcode(fp,"&");
                else
                    while (derefs-- != 0)
                        prcode(fp,"*");
            }

            prcode(fp, "%a", mod, ad, a);

            if (copy || isConstArg(ad))
                prcode(fp,")");

            if (isArray(ad))
                prcode(fp, ", (Py_ssize_t)%a", mod, &sd->args[arraylenarg], arraylenarg);

            if (ad->atype == mapped_type)
                prcode(fp, ", sipType_%T", ad);
            else if (ad->atype == fake_void_type || ad->atype == class_type)
                prcode(fp, ", sipType_%C", classFQCName(ad->u.cd));
            else
                prcode(fp,", sipType_QObject");

            if (!isArray(ad))
                prcode(fp, ", SIP_NULLPTR");
        }
        else if (ad->atype == capsule_type)
        {
            prcode(fp, ", \"%S\"", ad->u.cap);
        }
        else
        {
            if (!isArraySize(ad))
            {
                prcode(fp, ", ");

                while (derefs-- != 0)
                    prcode(fp, "*");

                prcode(fp, "%a", mod, ad, a);
            }

            if (isArray(ad))
                prcode(fp, ", (Py_ssize_t)%a", mod, &sd->args[arraylenarg], arraylenarg);
            else if (ad->atype == enum_type && ad->u.ed->fqcname != NULL)
                prcode(fp, ", sipType_%C", ad->u.ed->fqcname);
        }
    }
}


/*
 * Generate the library header #include directives required by either a class
 * or a module.
 */
static void generateUsedIncludes(ifaceFileList *iffl, FILE *fp)
{
    prcode(fp,
"\n"
        );

    while (iffl != NULL)
    {
        generateCppCodeBlock(iffl->iff->hdrcode, fp);
        iffl = iffl->next;
    }
}


/*
 * Generate the API details for a module.
 */
static void generateModuleAPI(sipSpec *pt, moduleDef *mod, FILE *fp)
{
    int no_exceptions = TRUE;
    classDef *cd;
    mappedTypeDef *mtd;
    exceptionDef *xd;
    virtErrorHandler *veh;

    for (cd = pt->classes; cd != NULL; cd = cd->next)
    {
        if (cd->iff->module == mod)
            generateClassAPI(cd, pt, fp);

        if (isExportDerived(cd))
        {
            generateCppCodeBlock(cd->iff->hdrcode, fp);
            generateShadowClassDeclaration(pt, cd, fp);
        }
    }

    for (mtd = pt->mappedtypes; mtd != NULL; mtd = mtd->next)
        if (mtd->iff->module == mod)
            generateMappedTypeAPI(pt, mtd, fp);

    for (xd = pt->exceptions; xd != NULL; xd = xd->next)
        if (xd->iff->module == mod && xd->exceptionnr >= 0)
        {
            if (no_exceptions)
            {
                prcode(fp,
"\n"
"/* The exceptions defined in this module. */\n"
"extern PyObject *sipExportedExceptions_%s[];\n"
"\n"
                    , mod->name);

                no_exceptions = FALSE;
            }

            prcode(fp,
"#define sipException_%C sipExportedExceptions_%s[%d]\n"
                , xd->iff->fqcname, mod->name, xd->exceptionnr);
        }

    generateEnumMacros(pt, mod, NULL, NULL, NULL, fp);

    for (veh = pt->errorhandlers; veh != NULL; veh = veh->next)
        if (veh->mod == mod)
            prcode(fp,
"\n"
"void sipVEH_%s_%s(sipSimpleWrapper *, sip_gilstate_t);\n"
                , mod->name, veh->name);
}


/*
 * Generate the API details for an imported module.
 */
static void generateImportedModuleAPI(sipSpec *pt, moduleDef *mod,
        moduleDef *immod, FILE *fp)
{
    classDef *cd;
    mappedTypeDef *mtd;
    exceptionDef *xd;

    for (cd = pt->classes; cd != NULL; cd = cd->next)
        if (cd->iff->module == immod)
        {
            if (cd->iff->needed)
                generateImportedClassAPI(cd, mod, fp);

            generateEnumMacros(pt, mod, cd, NULL, immod, fp);
        }

    for (mtd = pt->mappedtypes; mtd != NULL; mtd = mtd->next)
        if (mtd->iff->module == immod)
        {
            if (mtd->iff->needed)
                generateImportedMappedTypeAPI(mtd, mod, fp);

            generateEnumMacros(pt, mod, NULL, mtd, immod, fp);
        }

    for (xd = pt->exceptions; xd != NULL; xd = xd->next)
        if (xd->iff->module == immod && xd->exceptionnr >= 0)
                prcode(fp,
"\n"
"#define sipException_%C sipImportedExceptions_%s_%s[%d].iexc_object\n"
                    , xd->iff->fqcname, mod->name, xd->iff->module->name, xd->exceptionnr);

    generateEnumMacros(pt, mod, NULL, NULL, immod, fp);
}


/*
 * Generate the API details for an imported mapped type.  This will only be
 * called for the first API implementation.
 */
static void generateImportedMappedTypeAPI(mappedTypeDef *mtd, moduleDef *mod,
        FILE *fp)
{
    const char *mname = mod->name;
    const char *imname = mtd->iff->module->name;
    argDef type;

    memset(&type, 0, sizeof (argDef));

    type.atype = mapped_type;
    type.u.mtd = mtd;

    prcode(fp,
"\n"
"#define sipType_%T sipImportedTypes_%s_%s[%d].it_td\n"
        , &type, mname, imname, mtd->iff->ifacenr);
}


/*
 * Generate the API details for a mapped type.
 */
static void generateMappedTypeAPI(sipSpec *pt, mappedTypeDef *mtd, FILE *fp)
{
    argDef type;

    memset(&type, 0, sizeof (argDef));

    type.atype = mapped_type;
    type.u.mtd = mtd;

    prcode(fp,
"\n"
"#define sipType_%T sipExportedTypes_%s[%d]\n"
"\n"
"extern sipMappedTypeDef sipTypeDef_%s_%L;\n"
        , &type, mtd->iff->module->name, mtd->iff->ifacenr
        , mtd->iff->module->name, mtd->iff);

    generateEnumMacros(pt, mtd->iff->module, NULL, mtd, NULL, fp);
}


/*
 * Generate the API details for an imported class.  This will only be called
 * for the first API implementation.
 */
static void generateImportedClassAPI(classDef *cd, moduleDef *mod, FILE *fp)
{
    const char *mname = mod->name;
    const char *imname = cd->iff->module->name;

    prcode(fp,
"\n"
        );

    if (cd->iff->type == namespace_iface)
        prcode(fp,
"#if !defined(sipType_%L)\n"
            , cd->iff);

    prcode(fp,
"#define sipType_%C sipImportedTypes_%s_%s[%d].it_td\n"
        , classFQCName(cd), mname, imname, cd->iff->ifacenr);

    if (cd->iff->type == namespace_iface)
        prcode(fp,
"#endif\n"
            );
}


/*
 * Generate the C++ API for a class.
 */
static void generateClassAPI(classDef *cd, sipSpec *pt, FILE *fp)
{
    const char *mname = cd->iff->module->name;

    prcode(fp,
"\n"
            );

    if (cd->real == NULL && !isHiddenNamespace(cd))
        prcode(fp,
"#define sipType_%C sipExportedTypes_%s[%d]\n"
            , classFQCName(cd), mname, cd->iff->ifacenr);

    generateEnumMacros(pt, cd->iff->module, cd, NULL, NULL, fp);

    if (!isExternal(cd) && !isHiddenNamespace(cd))
        prcode(fp,
"\n"
"extern sipClassTypeDef sipTypeDef_%s_%L;\n"
            , mname, cd->iff);
}


/*
 * Generate the type macros for enums.
 */
static void generateEnumMacros(sipSpec *pt, moduleDef *mod, classDef *cd,
        mappedTypeDef *mtd, moduleDef *imported_module, FILE *fp)
{
    enumDef *ed;

    for (ed = pt->enums; ed != NULL; ed = ed->next)
    {
        if (ed->fqcname == NULL)
            continue;

        if (cd != NULL)
        {
            if (ed->ecd != cd)
                continue;
        }
        else if (mtd != NULL)
        {
            if (ed->emtd != mtd)
                continue;
        }
        else if (ed->ecd != NULL || ed->emtd != NULL)
        {
            continue;
        }

        if (imported_module == NULL)
        {
            if (mod == ed->module)
                prcode(fp,
"\n"
"#define sipType_%C sipExportedTypes_%s[%d]\n"
                    , ed->fqcname, mod->name, ed->enumnr);
        }
        else if (ed->module == imported_module && needsEnum(ed))
        {
            prcode(fp,
"\n"
"#define sipType_%C sipImportedTypes_%s_%s[%d].it_td\n"
                , ed->fqcname, mod->name, ed->module->name, ed->enumnr);
        }
    }
}


/*
 * Generate the shadow class declaration.
 */
static void generateShadowClassDeclaration(sipSpec *pt,classDef *cd,FILE *fp)
{
    int noIntro, nrVirts;
    ctorDef *ct;
    virtOverDef *vod;
    classDef *pcd;

    prcode(fp,
"\n"
"\n"
"class sip%C : public %U\n"
"{\n"
"public:\n"
        , classFQCName(cd), cd);

    /* Define a shadow class for any protected classes we have. */

    for (pcd = pt->classes; pcd != NULL; pcd = pcd->next)
    {
        mroDef *mro;

        if (!isProtectedClass(pcd))
            continue;

        /* See if the class defining the class is in our class hierachy. */
        for (mro = cd->mro; mro != NULL; mro = mro->next)
            if (mro->cd == pcd->ecd)
                break;

        if (mro == NULL)
            continue;

        prcode(fp,
"    class sip%s : public %s {\n"
"    public:\n"
                , classBaseName(pcd), classBaseName(pcd));

        generateProtectedEnums(pt, pcd, fp);

        prcode(fp,
"    };\n"
"\n"
                );
    }

    /* The constructor declarations. */

    for (ct = cd->ctors; ct != NULL; ct = ct->next)
    {
        ctorDef *dct;

        if (isPrivateCtor(ct))
            continue;

        if (ct->cppsig == NULL)
            continue;

        /* Check we haven't already handled this C++ signature. */
        for (dct = cd->ctors; dct != ct; dct = dct->next)
            if (dct->cppsig != NULL && sameSignature(dct->cppsig, ct->cppsig, TRUE))
                break;

        if (dct != ct)
            continue;

        prcode(fp,
"    sip%C(",classFQCName(cd));

        generateCalledArgs(NULL, cd->iff, ct->cppsig, Declaration, fp);

        prcode(fp,")%X;\n"
            ,ct->exceptions);
    }

    /* The destructor. */

    if (!isPrivateDtor(cd))
        prcode(fp,
"    %s~sip%C()%X;\n"
            ,(cd->vmembers != NULL ? "virtual " : ""),classFQCName(cd),cd->dtorexceptions);

    /* The metacall methods if required. */
    if ((pluginPyQt5(pt) || pluginPyQt6(pt)) && isQObjectSubClass(cd))
    {
        prcode(fp,
"\n"
"    int qt_metacall(QMetaObject::Call, int, void **) SIP_OVERRIDE;\n"
"    void *qt_metacast(const char *) SIP_OVERRIDE;\n"
            );

        if (!noPyQtQMetaObject(cd))
            prcode(fp,
"    const QMetaObject *metaObject() const SIP_OVERRIDE;\n"
                );
    }

    /* The exposure of protected enums. */

    generateProtectedEnums(pt,cd,fp);

    /* The wrapper around each protected member function. */

    generateProtectedDeclarations(cd,fp);

    /* The catcher around each virtual function in the hierarchy. */
    noIntro = TRUE;

    for (vod = cd->vmembers; vod != NULL; vod = vod->next)
    {
        overDef *od = vod->od;
        virtOverDef *dvod;

        if (isPrivate(od))
            continue;

        /* Check we haven't already handled this C++ signature. */
        for (dvod = cd->vmembers; dvod != vod; dvod = dvod->next)
            if (strcmp(dvod->od->cppname,od->cppname) == 0 && sameSignature(dvod->od->cppsig,od->cppsig,TRUE))
                break;

        if (dvod != vod)
            continue;

        if (noIntro)
        {
            prcode(fp,
"\n"
"    /*\n"
"     * There is a protected method for every virtual method visible from\n"
"     * this class.\n"
"     */\n"
"protected:\n"
                );

            noIntro = FALSE;
        }

        prcode(fp,
"    ");
 
        generateOverloadDecl(fp, cd->iff, od);
        prcode(fp, ";\n");
    }

    prcode(fp,
"\n"
"public:\n"
"    sipSimpleWrapper *sipPySelf;\n"
        );

    /* The private declarations. */

    prcode(fp,
"\n"
"private:\n"
"    sip%C(const sip%C &);\n"
"    sip%C &operator = (const sip%C &);\n"
        ,classFQCName(cd),classFQCName(cd)
        ,classFQCName(cd),classFQCName(cd));

    if ((nrVirts = countVirtuals(cd)) > 0)
        prcode(fp,
"\n"
"    char sipPyMethods[%d];\n"
            ,nrVirts);

    prcode(fp,
"};\n"
        );
}


/*
 * Generate the C++ declaration for an overload.
 */
static void generateOverloadDecl(FILE *fp, ifaceFileDef *scope, overDef *od)
{
    int a;
    argDef *res = &od->cppsig->result;

    /* Counter the handling of protected enums by generateBaseType(). */
    normaliseArg(res);
    generateBaseType(scope, res, TRUE, STRIP_NONE, fp);
    restoreArg(res);
 
    normaliseArgs(od->cppsig);

    prcode(fp, " %O(", od);

    for (a = 0; a < od->cppsig->nrArgs; ++a)
    {
        argDef *ad = &od->cppsig->args[a];

        if (a > 0)
            prcode(fp, ", ");

        generateBaseType(scope, ad, TRUE, STRIP_NONE, fp);
    }
 
    prcode(fp, ")%s%X SIP_OVERRIDE", (isConst(od) ? " const" : ""), od->exceptions);

    restoreArgs(od->cppsig);
}


/*
 * Generate typed arguments for a declaration or a definition.
 */
static void generateCalledArgs(moduleDef *mod, ifaceFileDef *scope,
        signatureDef *sd, funcArgType ftype, FILE *fp)
{
    const char *name;
    int a;

    for (a = 0; a < sd->nrArgs; ++a)
    {
        argDef *ad = &sd->args[a];

        if (a > 0)
            prcode(fp, ", ");

        if (ftype == Definition)
            name = get_argument_name(ad, a, mod);
        else
            name = "";

        generateNamedBaseType(scope, ad, name, TRUE, STRIP_NONE, fp);
    }
}


/*
 * Generate typed arguments for a call.
 */
static void generateCallArgs(moduleDef *mod, signatureDef *sd,
        signatureDef *py_sd, FILE *fp)
{
    int a;

    for (a = 0; a < sd->nrArgs; ++a)
    {
        char *ind = NULL;
        argDef *ad, *py_ad;

        if (a > 0)
            prcode(fp,", ");

        ad = &sd->args[a];

        /* See if the argument needs dereferencing or it's address taking. */
        switch (ad->atype)
        {
        case ascii_string_type:
        case latin1_string_type:
        case utf8_string_type:
        case sstring_type:
        case ustring_type:
        case string_type:
        case wstring_type:
            if (ad->nrderefs > (isOutArg(ad) ? 0 : 1) && !isReference(ad))
                ind = "&";

            break;

        case mapped_type:
        case class_type:
            if (ad->nrderefs == 2)
                ind = "&";
            else if (ad->nrderefs == 0)
                ind = "*";

            break;

        case struct_type:
        case union_type:
        case void_type:
            if (ad->nrderefs == 2)
                ind = "&";

            break;

        default:
            if (ad->nrderefs == 1)
                ind = "&";
        }

        /*
         * See if we need to cast a Python void * to the correct C/C++ pointer
         * type.
         */
        if (py_sd != sd)
        {
            py_ad = &py_sd->args[a];

            if ((py_ad->atype != void_type && py_ad->atype != capsule_type) || ad->atype == void_type || ad->atype == capsule_type || py_ad->nrderefs != ad->nrderefs)
                py_ad = NULL;
        }
        else
            py_ad = NULL;

        if (py_ad == NULL)
        {
            if (ind != NULL)
                prcode(fp, ind);

            if (isArraySize(ad))
                prcode(fp, "(%b)", ad);

            prcode(fp, "%a", mod, ad, a);
        }
        else if (generating_c)
            prcode(fp, "(%b *)%a", ad, mod, ad, a);
        else
            prcode(fp, "reinterpret_cast<%b *>(%a)", ad, mod, ad, a);
    }
}


/*
 * Generate the declaration of a named variable to hold a result from a C++
 * function call.
 */
static void generateNamedValueType(ifaceFileDef *scope, argDef *ad,
        char *name, FILE *fp)
{
    argDef mod = *ad;

    if (ad->nrderefs == 0)
    {
        if (ad->atype == class_type || ad->atype == mapped_type)
            mod.nrderefs = 1;
        else
            resetIsConstArg(&mod);
    }

    resetIsReference(&mod);
    generateNamedBaseType(scope, &mod, name, TRUE, STRIP_NONE, fp);
}


/*
 * Generate a C++ type.
 */
void generateBaseType(ifaceFileDef *scope, argDef *ad, int use_typename,
        int strip, FILE *fp)
{
    generateNamedBaseType(scope, ad, "", use_typename, strip, fp);
}


/*
 * Generate a C++ type and name.
 */
static void generateNamedBaseType(ifaceFileDef *scope, argDef *ad,
        const char *name, int use_typename, int strip, FILE *fp)
{
    typedefDef *td = ad->original_type;
    int nr_derefs = ad->nrderefs;
    int is_reference = isReference(ad);
    int i, space_before_name;

    if (use_typename && td != NULL && !noTypeName(td) && !isArraySize(ad))
    {
        if (isConstArg(ad) && !isConstArg(&td->type))
            prcode(fp, "const ");

        nr_derefs -= td->type.nrderefs;

        if (isReference(&td->type))
            is_reference = FALSE;

        prcode(fp, "%S", stripScope(td->fqname, strip));
    }
    else
    {
        /*
         * A function type is handled differently because of the position of
         * the name.
         */
        if (ad->atype == function_type)
        {
            signatureDef *sig = ad->u.sa;

            generateBaseType(scope, &sig->result, TRUE, strip, fp);

            prcode(fp," (");

            for (i = 0; i < nr_derefs; ++i)
                prcode(fp, "*");

            prcode(fp, "%s)(",name);
            generateCalledArgs(NULL, scope, sig, Declaration, fp);
            prcode(fp, ")");

            return;
        }

        if (isConstArg(ad))
            prcode(fp, "const ");

        switch (ad->atype)
        {
        case sbyte_type:
        case sstring_type:
            prcode(fp, "signed char");
            break;

        case ubyte_type:
        case ustring_type:
            prcode(fp, "unsigned char");
            break;

        case wstring_type:
            prcode(fp, "wchar_t");
            break;

        case byte_type:
        case ascii_string_type:
        case latin1_string_type:
        case utf8_string_type:
        case string_type:
            prcode(fp, "char");
            break;

        case ushort_type:
            prcode(fp, "unsigned short");
            break;

        case short_type:
            prcode(fp, "short");
            break;

        case uint_type:
            /*
             * Qt4 moc uses "uint" in signal signatures.  We do all the time
             * and hope it is always defined.
             */
            prcode(fp, "uint");
            break;

        case int_type:
        case cint_type:
            prcode(fp, "int");
            break;

        case hash_type:
            prcode(fp, "Py_hash_t");
            break;

        case ssize_type:
            prcode(fp, "Py_ssize_t");
            break;

        case size_type:
            prcode(fp, "size_t");
            break;

        case ulong_type:
            prcode(fp, "unsigned long");
            break;

        case long_type:
            prcode(fp, "long");
            break;

        case ulonglong_type:
            prcode(fp, "unsigned long long");
            break;

        case longlong_type:
            prcode(fp, "long long");
            break;

        case struct_type:
            prcode(fp, "struct %S", ad->u.sname);
            break;

        case union_type:
            prcode(fp, "union %S", ad->u.sname);
            break;

        case capsule_type:
            nr_derefs = 1;

            /* Drop through. */

        case fake_void_type:
        case void_type:
            prcode(fp, "void");
            break;

        case bool_type:
        case cbool_type:
            prcode(fp, "bool");
            break;

        case float_type:
        case cfloat_type:
            prcode(fp, "float");
            break;

        case double_type:
        case cdouble_type:
            prcode(fp, "double");
            break;

        case defined_type:
            /*
             * The only defined types still remaining are arguments to
             * templates and default values.
             */
            if (prcode_xml)
            {
                prScopedName(fp, removeGlobalScope(ad->u.snd), ".");
            }
            else
            {
                if (generating_c)
                    fprintf(fp, "struct ");

                prScopedName(fp, stripScope(ad->u.snd, strip), "::");
            }

            break;

        case mapped_type:
            generateBaseType(scope, &ad->u.mtd->type, TRUE, strip, fp);
            break;

        case class_type:
            prScopedClassName(fp, scope, ad->u.cd, strip);
            break;

        case template_type:
			prTemplateType(fp, scope, ad->u.td, strip);
			break;

        case enum_type:
            {
                enumDef *ed = ad->u.ed;

                if (ed->fqcname == NULL || isProtectedEnum(ed))
                    fprintf(fp,"int");
                else
                    prScopedName(fp, stripScope(ed->fqcname, strip), "::");

                break;
            }

        case pyobject_type:
        case pytuple_type:
        case pylist_type:
        case pydict_type:
        case pycallable_type:
        case pyslice_type:
        case pytype_type:
        case pybuffer_type:
        case pyenum_type:
        case ellipsis_type:
            prcode(fp, "PyObject *");
            break;

        /* Supress a compiler warning. */
        default:
            ;
        }
    }

    space_before_name = TRUE;

    for (i = 0; i < nr_derefs; ++i)
    {
        /*
         * Note that we don't put a space before the '*' so that Qt normalised
         * signal signatures are correct.
         */
        prcode(fp, "*");
        space_before_name = FALSE;

        if (ad->derefs[i])
        {
            prcode(fp, " const");
            space_before_name = TRUE;
        }
    }

    if (is_reference)
        prcode(fp, (prcode_xml ? "&amp;" : "&"));

    if (*name != '\0')
    {
        if (space_before_name)
            prcode(fp, " ");

        prcode(fp, name);
    }
}


/*
 * Generate the definition of an argument variable and any supporting
 * variables.
 */
static void generateVariable(moduleDef *mod, ifaceFileDef *scope, argDef *ad,
        int argnr, FILE *fp)
{
    argType atype = ad->atype;
    argDef orig;

    if (isInArg(ad) && ad->defval != NULL &&
        (atype == class_type || atype == mapped_type) &&
        (ad->nrderefs == 0 || isReference(ad)))
    {
        /*
         * Generate something to hold the default value as it cannot be
         * assigned straight away.
         */
        prcode(fp,
"        %A %adef = ", scope, ad, mod, ad, argnr);

        generateExpression(ad->defval, FALSE, fp);

        prcode(fp,";\n"
            );
    }

    /* Adjust the type so we have the type that will really handle it. */

    orig = *ad;

    switch (atype)
    {
    case ascii_string_type:
    case latin1_string_type:
    case utf8_string_type:
    case sstring_type:
    case ustring_type:
    case string_type:
    case wstring_type:
        if (!isReference(ad))
        {
            if (ad->nrderefs == 2)
                ad->nrderefs = 1;
            else if (ad->nrderefs == 1 && isOutArg(ad))
                ad->nrderefs = 0;
        }

        break;

    case mapped_type:
    case class_type:
    case struct_type:
    case union_type:
    case void_type:
        ad->nrderefs = 1;
        break;

    default:
        ad->nrderefs = 0;
    }

    /* Array sizes are always Py_ssize_t. */
    if (isArraySize(ad))
        ad->atype = ssize_type;

    resetIsReference(ad);

    if (ad->nrderefs == 0)
        resetIsConstArg(ad);

    prcode(fp,
"        %A %a", scope, ad, mod, ad, argnr);

    *ad = orig;

    generateDefaultValue(mod, ad, argnr, fp);

    prcode(fp,";\n"
        );

    /* Some types have supporting variables. */
    if (isInArg(ad))
    {
        if (isGetWrapper(ad))
            prcode(fp,
"        PyObject *%aWrapper%s;\n"
                , mod, ad, argnr, (ad->defval != NULL ? " = 0" : ""));
        else if (keepReference(ad))
            prcode(fp,
"        PyObject *%aKeep%s;\n"
                , mod, ad, argnr, (ad->defval != NULL ? " = 0" : ""));

        switch (atype)
        {
        case class_type:
            if (isArray(ad) && abiSupportsArray())
            {
                prcode(fp,
"        int %aIsTemp = 0;\n"
                    , mod, ad, argnr);
            }
            else if (!isArray(ad) && ad->u.cd->convtocode != NULL && !isConstrained(ad))
            {
                prcode(fp,
"        int %aState = 0;\n"
                    , mod, ad, argnr);

                if (typeNeedsUserState(ad))
                    prcode(fp,
"        void *%aUserState = SIP_NULLPTR;\n"
                        , mod, ad, argnr);
            }

            break;

        case mapped_type:
            if (!noRelease(ad->u.mtd) && !isConstrained(ad))
            {
                prcode(fp,
"        int %aState = 0;\n"
                    , mod, ad, argnr);

                if (typeNeedsUserState(ad))
                    prcode(fp,
"        void *%aUserState = SIP_NULLPTR;\n"
                        , mod, ad, argnr);
            }

            break;

        case ascii_string_type:
        case latin1_string_type:
        case utf8_string_type:
            if (!keepReference(ad) && ad->nrderefs == 1)
                prcode(fp,
"        PyObject *%aKeep%s;\n"
                    , mod, ad, argnr, (ad->defval != NULL ? " = 0" : ""));

            break;

        /* Supress a compiler warning. */
        default:
            ;
        }
    }
}


/*
 * Generate a default value.
 */
static void generateDefaultValue(moduleDef *mod, argDef *ad, int argnr,
        FILE *fp)
{
    if (isInArg(ad) && ad->defval != NULL)
    {
        prcode(fp," = ");

        if ((ad->atype == class_type || ad->atype == mapped_type) &&
                (ad->nrderefs == 0 || isReference(ad)))
            prcode(fp, "&%adef", mod, ad, argnr);
        else
            generateExpression(ad->defval, FALSE, fp);
    }
}


/*
 * Generate a simple function call.
 */
static void generateSimpleFunctionCall(fcallDef *fcd, int in_str, FILE *fp)
{
    int i;

    prcode(fp, "%B(", &fcd->type);

    for (i = 0; i < fcd->nrArgs; ++i)
    {
        if (i > 0)
            prcode(fp,", ");

        generateExpression(fcd->args[i], in_str, fp);
    }

    prcode(fp,")");
}


/*
 * Generate the type structure that contains all the information needed by the
 * meta-type.  A sub-set of this is used to extend namespaces.
 */
static int generateTypeDefinition(sipSpec *pt, classDef *cd, int py_debug,
        FILE *fp)
{
    const char *sep;
    int is_slots, nr_methods, nr_enums, nr_vars, plugin;
    int is_inst_class, is_inst_voidp, is_inst_char, is_inst_string;
    int is_inst_int, is_inst_long, is_inst_ulong, is_inst_longlong;
    int is_inst_ulonglong, is_inst_double, has_docstring;
    memberDef *md;
    moduleDef *mod;
    propertyDef *pd;

    mod = cd->iff->module;

    if (cd->supers != NULL)
    {
        classList *cl;

        prcode(fp,
"\n"
"\n"
"/* Define this type's super-types. */\n"
"static sipEncodedTypeDef supers_%C[] = {", classFQCName(cd));

        for (cl = cd->supers; cl != NULL; cl = cl->next)
        {
            if (cl != cd->supers)
                prcode(fp, ", ");

            generateEncodedType(mod, cl->cd, (cl->next == NULL), fp);
        }

        prcode(fp,"};\n"
            );
    }

    /* Generate the slots table. */
    is_slots = FALSE;

    for (md = cd->members; md != NULL; md = md->next)
    {
        const char *stype;

        if (md->slot == no_slot)
            continue;

        if (!is_slots)
        {
            prcode(fp,
"\n"
"\n"
"/* Define this type's Python slots. */\n"
"static sipPySlotDef slots_%L[] = {\n"
                , cd->iff);

            is_slots = TRUE;
        }

        if ((stype = slotName(md->slot)) != NULL)
        {
            prcode(fp,
"    {(void *)slot_%L_%s, %s},\n"
                , cd->iff, md->pyname->text, stype);
        }
    }

    if (is_slots)
        prcode(fp,
"    {0, (sipPySlotType)0}\n"
"};\n"
            );

    /* Generate the attributes tables. */
    nr_methods = generateClassMethodTable(pt, cd, fp);

    if (abiVersion >= ABI_13_0)
        nr_enums = -1;
    else
        nr_enums = generateEnumMemberTable(pt, mod, cd, NULL, fp);

    /* Generate the property and variable handlers. */
    nr_vars = 0;

    if (hasVarHandlers(cd))
    {
        varDef *vd;

        for (vd = pt->vars; vd != NULL; vd = vd->next)
            if (vd->ecd == cd && needsHandler(vd))
            {
                ++nr_vars;

                generateVariableGetter(cd->iff, vd, fp);

                if (canSetVariable(vd))
                    generateVariableSetter(cd->iff, vd, fp);
            }
    }

    /* Generate any docstrings. */
    for (pd = cd->properties; pd != NULL; pd = pd->next)
    {
        ++nr_vars;

        if (pd->docstring != NULL)
        {
            prcode(fp,
"\n"
"PyDoc_STRVAR(doc_%L_%s, \"" , cd->iff, pd->name->text);

            generateDocstringText(pd->docstring, fp);

            prcode(fp, "\");\n"
                );
        }
    }

    /* Generate the variables table. */
    if (nr_vars > 0)
        prcode(fp,
"\n"
"sipVariableDef variables_%L[] = {\n"
            , cd->iff);

    for (pd = cd->properties; pd != NULL; pd = pd->next)
    {
        prcode(fp,
"    {PropertyVariable, %N, &methods_%L[%d], ", pd->name, cd->iff, findMethod(cd, pd->get)->membernr);

        if (pd->set != NULL)
            prcode(fp, "&methods_%L[%d], ", cd->iff, findMethod(cd, pd->set)->membernr);
        else
            prcode(fp, "SIP_NULLPTR, ");

        /* We don't support a deleter yet. */
        prcode(fp, "SIP_NULLPTR, ");

        if (pd->docstring != NULL)
            prcode(fp, "doc_%L_%s", cd->iff, pd->name->text);
        else
            prcode(fp, "SIP_NULLPTR");

        prcode(fp, "},\n"
            );
    }

    if (hasVarHandlers(cd))
    {
        varDef *vd;

        for (vd = pt->vars; vd != NULL; vd = vd->next)
            if (vd->ecd == cd && needsHandler(vd))
            {
                prcode(fp,
"    {%s, %N, (PyMethodDef *)varget_%C, ", (isStaticVar(vd) ? "ClassVariable" : "InstanceVariable"), vd->pyname, vd->fqcname);

                if (canSetVariable(vd))
                    prcode(fp, "(PyMethodDef *)varset_%C", vd->fqcname);
                else
                    prcode(fp, "SIP_NULLPTR");

                prcode(fp, ", SIP_NULLPTR, SIP_NULLPTR},\n"
                    );
            }
    }

    if (nr_vars > 0)
        prcode(fp,
"};\n"
            );

    /* Generate each instance table. */
    is_inst_class = generateClasses(pt, mod, cd, fp);
    is_inst_voidp = generateVoidPointers(pt, mod, cd, fp);
    is_inst_char = generateChars(pt, mod, cd, fp);
    is_inst_string = generateStrings(pt, mod, cd, fp);
    is_inst_int = generateInts(pt, mod, cd->iff, fp);
    is_inst_long = generateLongs(pt, mod, cd, fp);
    is_inst_ulong = generateUnsignedLongs(pt, mod, cd, fp);
    is_inst_longlong = generateLongLongs(pt, mod, cd, fp);
    is_inst_ulonglong = generateUnsignedLongLongs(pt, mod, cd, fp);
    is_inst_double = generateDoubles(pt, mod, cd, fp);

    /* Generate the docstrings. */
    if (hasClassDocstring(pt, cd))
    {
        prcode(fp,
"\n"
"PyDoc_STRVAR(doc_%L, \"", cd->iff);

        generateClassDocstring(pt, cd, fp);

        prcode(fp, "\");\n"
            );

        has_docstring = TRUE;
    }
    else
    {
        has_docstring = FALSE;
    }

    /* Generate any plugin-specific data structures. */
    if (pluginPyQt5(pt) || pluginPyQt6(pt))
    {
        if ((plugin = generatePyQtClassPlugin(pt, cd, fp)) < 0)
            return -1;
    }
    else
    {
        plugin = FALSE;
    }

    prcode(fp,
"\n"
"\n"
"sipClassTypeDef ");

    generateTypeDefName(cd->iff, fp);

    prcode(fp, " = {\n"
"    {\n"
        );

    if (abiVersion < ABI_13_0)
        prcode(fp,
"        -1,\n"
"        SIP_NULLPTR,\n"
            );

    prcode(fp,
"        SIP_NULLPTR,\n"
"        ");

    sep = "";

    if (isAbstractClass(cd))
    {
        prcode(fp, "%sSIP_TYPE_ABSTRACT", sep);
        sep = "|";
    }

    if (cd->subbase != NULL)
    {
        prcode(fp, "%sSIP_TYPE_SCC", sep);
        sep = "|";
    }

    if (classHandlesNone(cd))
    {
        prcode(fp, "%sSIP_TYPE_ALLOW_NONE", sep);
        sep = "|";
    }

    if (hasNonlazyMethod(cd))
    {
        prcode(fp, "%sSIP_TYPE_NONLAZY", sep);
        sep = "|";
    }

    if (isCallSuperInitYes(mod))
    {
        prcode(fp, "%sSIP_TYPE_SUPER_INIT", sep);
        sep = "|";
    }

    if (!py_debug && useLimitedAPI(mod))
    {
        prcode(fp, "%sSIP_TYPE_LIMITED_API", sep);
        sep = "|";
    }

    if (cd->iff->type == namespace_iface)
    {
        prcode(fp, "%sSIP_TYPE_NAMESPACE", sep);
        sep = "|";
    }
    else
    {
        prcode(fp, "%sSIP_TYPE_CLASS", sep);
        sep = "|";
    }

    if (*sep == '\0')
        prcode(fp, "0");

    prcode(fp, ",\n");

    prcode(fp,
"        %n,\n"
"        SIP_NULLPTR,\n"
        , cd->iff->name);

    if (plugin)
        prcode(fp,
"        &plugin_%L,\n"
            , cd->iff);
    else
        prcode(fp,
"        SIP_NULLPTR,\n"
            );

    prcode(fp,
"    },\n"
"    {\n"
        );

    if (cd->real == NULL)
        prcode(fp,
"        %n,\n"
            , cd->pyname);
    else
        prcode(fp,
"        -1,\n"
            );

    prcode(fp, "        ");

    if (cd->real != NULL)
        generateEncodedType(mod, cd->real, 0, fp);
    else if (pyScope(cd->ecd) != NULL)
        generateEncodedType(mod, cd->ecd, 0, fp);
    else
        prcode(fp, "{0, 0, 1}");

    prcode(fp, ",\n"
        );

    if (nr_methods == 0)
        prcode(fp,
"        0, SIP_NULLPTR,\n"
            );
    else
        prcode(fp,
"        %d, methods_%L,\n"
            , nr_methods, cd->iff);

    if (nr_enums == 0)
        prcode(fp,
"        0, SIP_NULLPTR,\n"
            );
    else if (nr_enums > 0)
        prcode(fp,
"        %d, enummembers_%L,\n"
            , nr_enums, cd->iff);

    if (nr_vars == 0)
        prcode(fp,
"        0, SIP_NULLPTR,\n"
            );
    else
        prcode(fp,
"        %d, variables_%L,\n"
            , nr_vars, cd->iff);

    prcode(fp,
"        {");

    if (is_inst_class)
        prcode(fp, "typeInstances_%C, ", classFQCName(cd));
    else
        prcode(fp, "SIP_NULLPTR, ");

    if (is_inst_voidp)
        prcode(fp, "voidPtrInstances_%C, ", classFQCName(cd));
    else
        prcode(fp, "SIP_NULLPTR, ");

    if (is_inst_char)
        prcode(fp, "charInstances_%C, ", classFQCName(cd));
    else
        prcode(fp, "SIP_NULLPTR, ");

    if (is_inst_string)
        prcode(fp, "stringInstances_%C, ", classFQCName(cd));
    else
        prcode(fp, "SIP_NULLPTR, ");

    if (is_inst_int)
        prcode(fp, "intInstances_%C, ", classFQCName(cd));
    else
        prcode(fp, "SIP_NULLPTR, ");

    if (is_inst_long)
        prcode(fp, "longInstances_%C, ", classFQCName(cd));
    else
        prcode(fp, "SIP_NULLPTR, ");

    if (is_inst_ulong)
        prcode(fp, "unsignedLongInstances_%C, ", classFQCName(cd));
    else
        prcode(fp, "SIP_NULLPTR, ");

    if (is_inst_longlong)
        prcode(fp, "longLongInstances_%C, ", classFQCName(cd));
    else
        prcode(fp, "SIP_NULLPTR, ");

    if (is_inst_ulonglong)
        prcode(fp, "unsignedLongLongInstances_%C, ", classFQCName(cd));
    else
        prcode(fp, "SIP_NULLPTR, ");

    if (is_inst_double)
        prcode(fp, "doubleInstances_%C", classFQCName(cd));
    else
        prcode(fp, "SIP_NULLPTR");

    prcode(fp,"},\n"
"    },\n"
        );

    if (has_docstring)
        prcode(fp,
"    doc_%L,\n"
            , cd->iff);
    else
        prcode(fp,
"    SIP_NULLPTR,\n"
            );

    if (cd->metatype != NULL)
        prcode(fp,
"    %n,\n"
            , cd->metatype);
    else
        prcode(fp,
"    -1,\n"
            );

    if (cd->supertype != NULL)
        prcode(fp,
"    %n,\n"
            , cd->supertype);
    else
        prcode(fp,
"    -1,\n"
            );

    if (cd->supers != NULL)
        prcode(fp,
"    supers_%C,\n"
            , classFQCName(cd));
    else
        prcode(fp,
"    SIP_NULLPTR,\n"
            );

    if (is_slots)
        prcode(fp,
"    slots_%L,\n"
            , cd->iff);
    else
        prcode(fp,
"    SIP_NULLPTR,\n"
            );

    if (canCreate(cd))
        prcode(fp,
"    init_type_%L,\n"
            , cd->iff);
    else
        prcode(fp,
"    SIP_NULLPTR,\n"
            );

    if (cd->travcode != NULL)
        prcode(fp,
"    traverse_%C,\n"
            , classFQCName(cd));
    else
        prcode(fp,
"    SIP_NULLPTR,\n"
            );

    if (cd->clearcode != NULL)
        prcode(fp,
"    clear_%C,\n"
            , classFQCName(cd));
    else
        prcode(fp,
"    SIP_NULLPTR,\n"
            );

    if (cd->getbufcode != NULL)
        prcode(fp,
"    getbuffer_%C,\n"
            , classFQCName(cd));
    else
        prcode(fp,
"    SIP_NULLPTR,\n"
            );

    if (cd->releasebufcode != NULL)
        prcode(fp,
"    releasebuffer_%C,\n"
            , classFQCName(cd));
    else
        prcode(fp,
"    SIP_NULLPTR,\n"
            );

    if (needDealloc(cd))
        prcode(fp,
"    dealloc_%L,\n"
            , cd->iff);
    else
        prcode(fp,
"    SIP_NULLPTR,\n"
            );

    if (generating_c || copyHelper(cd))
        prcode(fp,
"    assign_%L,\n"
            , cd->iff);
    else
        prcode(fp,
"    SIP_NULLPTR,\n"
            );

    if (generating_c || arrayHelper(cd))
        prcode(fp,
"    array_%L,\n"
            , cd->iff);
    else
        prcode(fp,
"    SIP_NULLPTR,\n"
            );

    if (generating_c || copyHelper(cd))
        prcode(fp,
"    copy_%L,\n"
            , cd->iff);
    else
        prcode(fp,
"    SIP_NULLPTR,\n"
            );

    if (cd->iff->type == namespace_iface || generating_c)
        prcode(fp,
"    SIP_NULLPTR,\n"
            );
    else
        prcode(fp,
"    release_%L,\n"
            , cd->iff);

    if (cd->supers != NULL)
        prcode(fp,
"    cast_%L,\n"
            , cd->iff);
    else
        prcode(fp,
"    SIP_NULLPTR,\n"
            );

    if (cd->iff->type == namespace_iface)
    {
        prcode(fp,
"    SIP_NULLPTR,\n"
            );
    }
    else
    {
        if (cd->convtocode != NULL)
            prcode(fp,
"    convertTo_%L,\n"
                , cd->iff);
        else
            prcode(fp,
"    SIP_NULLPTR,\n"
                );
    }

    if (cd->iff->type == namespace_iface)
    {
        prcode(fp,
"    SIP_NULLPTR,\n"
            );
    }
    else
    {
        if (cd->convfromcode != NULL)
            prcode(fp,
"    convertFrom_%L,\n"
                , cd->iff);
        else
            prcode(fp,
"    SIP_NULLPTR,\n"
                );
    }

    prcode(fp,
"    SIP_NULLPTR,\n"
        );

    if (cd->picklecode != NULL)
        prcode(fp,
"    pickle_%C,\n"
            , classFQCName(cd));
    else
        prcode(fp,
"    SIP_NULLPTR,\n"
            );

    if (cd->finalcode != NULL)
        prcode(fp,
"    final_%C,\n"
            , classFQCName(cd));
    else
        prcode(fp,
"    SIP_NULLPTR,\n"
            );

    if (isMixin(cd))
        prcode(fp,
"    mixin_%C,\n"
            , classFQCName(cd));
    else
        prcode(fp,
"    SIP_NULLPTR,\n"
            );

    if (abiSupportsArray())
    {
        if (generating_c || arrayHelper(cd))
            prcode(fp,
"    array_delete_%L,\n"
                , cd->iff);
        else
            prcode(fp,
"    SIP_NULLPTR,\n"
                );

        if (canCreate(cd))
            prcode(fp,
"    sizeof (%U),\n"
                , cd);
        else
            prcode(fp,
"    0,\n"
                );
    }

    prcode(fp,
"};\n"
        );

    return 0;
}


/*
 * See if an overload has optional arguments.
 */
static int hasOptionalArgs(overDef *od)
{
    return (od->cppsig->nrArgs > 0 && od->cppsig->args[od->cppsig->nrArgs - 1].defval != NULL);
}


/*
 * Generate the PyQt emitters for a class.
 */
static int generatePyQtEmitters(classDef *cd, FILE *fp)
{
    moduleDef *mod = cd->iff->module;
    memberDef *md;

    for (md = cd->members; md != NULL; md = md->next)
    {
        int in_emitter = FALSE;
        overDef *od;

        for (od = cd->overs; od != NULL; od = od->next)
        {
            if (od->common != md || !isSignal(od) || !hasOptionalArgs(od))
                continue;

            if (!in_emitter)
            {
                in_emitter = TRUE;

                prcode(fp,
"\n"
"\n"
                    );

                if (!generating_c)
                    prcode(fp,
"extern \"C\" {static int emit_%L_%s(void *, PyObject *);}\n"
"\n"
                        , cd->iff, od->cppname);

                prcode(fp,
"static int emit_%L_%s(void *sipCppV, PyObject *sipArgs)\n"
"{\n"
"    PyObject *sipParseErr = SIP_NULLPTR;\n"
"    %U *sipCpp = reinterpret_cast<%U *>(sipCppV);\n"
                    , cd->iff, od->cppname
                    , cd, cd);
            }

            /*
             * Generate the code that parses the args and emits the appropriate
             * overloaded signal.
             */
            prcode(fp,
"\n"
"    {\n"
                );

            if (generateArgParser(mod, &od->pysig, cd, NULL, NULL, NULL, fp) < 0)
                return -1;

            prcode(fp,
"        {\n"
"            Py_BEGIN_ALLOW_THREADS\n"
"            sipCpp->%s("
                , od->cppname);

            generateCallArgs(mod, od->cppsig, &od->pysig, fp);

            prcode(fp, ");\n"
"            Py_END_ALLOW_THREADS\n"
"\n"
                );

            deleteTemps(mod, &od->pysig, fp);

            prcode(fp,
"\n"
"            return 0;\n"
"        }\n"
"    }\n"
            );
        }

        if (in_emitter)
        {
            prcode(fp,
"\n"
"    sipNoMethod(sipParseErr, %N, %N, SIP_NULLPTR);\n"
"\n"
"    return -1;\n"
"}\n"
                , cd->pyname, md->pyname);
        }
    }

    return 0;
}


/*
 * Generate an entry in the PyQt signal table.
 */
static void generateSignalTableEntry(sipSpec *pt, classDef *cd, overDef *sig,
        int membernr, int optional_args, FILE *fp)
{
    int a, stripped;

    prcode(fp,
"    {\"%s(", sig->cppname);

    stripped = FALSE;

    for (a = 0; a < sig->cppsig->nrArgs; ++a)
    {
        argDef arg = sig->cppsig->args[a];

        /* Note the lack of a separating space. */
        if (a > 0)
            prcode(fp,",");

        normaliseSignalArg(&arg);

        if (arg.scopes_stripped)
        {
            generateNamedBaseType(cd->iff, &arg, "", TRUE, arg.scopes_stripped,
                    fp);
            stripped = TRUE;
        }
        else
        {
            generateNamedBaseType(cd->iff, &arg, "", TRUE, STRIP_GLOBAL, fp);
        }
    }

    prcode(fp, ")");

    /*
     * If a scope was stripped then append an unstripped version which can be
     * parsed by PyQt.
     */
    if (stripped)
    {
        prcode(fp, "|(");

        for (a = 0; a < sig->cppsig->nrArgs; ++a)
        {
            argDef arg = sig->cppsig->args[a];

            /* Note the lack of a separating space. */
            if (a > 0)
                prcode(fp,",");

            normaliseSignalArg(&arg);

            generateNamedBaseType(cd->iff, &arg, "", TRUE, STRIP_GLOBAL, fp);
        }

        prcode(fp, ")");
    }

    prcode(fp, "\", ");

    if (docstrings)
    {
        prcode(fp, "\"");

        if (sig->docstring != NULL)
        {
            if (sig->docstring->signature == prepended)
            {
                dsOverload(pt, sig, TRUE, fp);
                prcode(fp, "\\n");
            }

            generateDocstringText(sig->docstring, fp);

            if (sig->docstring->signature == appended)
            {
                prcode(fp, "\\n");
                dsOverload(pt, sig, TRUE, fp);
            }
        }
        else
        {
            fprintf(fp, "\\1");
            dsOverload(pt, sig, TRUE, fp);
        }

        fprintf(fp, "\", ");
    }
    else
    {
        prcode(fp, "SIP_NULLPTR, ");
    }

    if (membernr >= 0)
        prcode(fp, "&methods_%L[%d], ", cd->iff, membernr);
    else
        prcode(fp, "SIP_NULLPTR, ");

    if (optional_args)
        prcode(fp, "emit_%L_%s", cd->iff, sig->cppname);
    else
        prcode(fp, "SIP_NULLPTR");

    prcode(fp,"},\n"
        );
}


/*
 * Do some signal argument normalisation so that Qt doesn't have to.
 */
static void normaliseSignalArg(argDef *ad)
{
    if (isConstArg(ad) && (isReference(ad) || ad->nrderefs == 0))
    {
        resetIsConstArg(ad);
        resetIsReference(ad);
    }
}


/*
 * Return the sip module's string equivalent of a slot.
 */
static const char *slotName(slotType st)
{
    const char *sn;

    switch (st)
    {
    case str_slot:
        sn = "str_slot";
        break;

    case int_slot:
        sn = "int_slot";
        break;

    case float_slot:
        sn = "float_slot";
        break;

    case len_slot:
        sn = "len_slot";
        break;

    case contains_slot:
        sn = "contains_slot";
        break;

    case add_slot:
        sn = "add_slot";
        break;

    case concat_slot:
        sn = "concat_slot";
        break;

    case sub_slot:
        sn = "sub_slot";
        break;

    case mul_slot:
        sn = "mul_slot";
        break;

    case repeat_slot:
        sn = "repeat_slot";
        break;

    case mod_slot:
        sn = "mod_slot";
        break;

    case floordiv_slot:
        sn = "floordiv_slot";
        break;

    case truediv_slot:
        sn = "truediv_slot";
        break;

    case and_slot:
        sn = "and_slot";
        break;

    case or_slot:
        sn = "or_slot";
        break;

    case xor_slot:
        sn = "xor_slot";
        break;

    case lshift_slot:
        sn = "lshift_slot";
        break;

    case rshift_slot:
        sn = "rshift_slot";
        break;

    case iadd_slot:
        sn = "iadd_slot";
        break;

    case iconcat_slot:
        sn = "iconcat_slot";
        break;

    case isub_slot:
        sn = "isub_slot";
        break;

    case imul_slot:
        sn = "imul_slot";
        break;

    case irepeat_slot:
        sn = "irepeat_slot";
        break;

    case imod_slot:
        sn = "imod_slot";
        break;

    case ifloordiv_slot:
        sn = "ifloordiv_slot";
        break;

    case itruediv_slot:
        sn = "itruediv_slot";
        break;

    case iand_slot:
        sn = "iand_slot";
        break;

    case ior_slot:
        sn = "ior_slot";
        break;

    case ixor_slot:
        sn = "ixor_slot";
        break;

    case ilshift_slot:
        sn = "ilshift_slot";
        break;

    case irshift_slot:
        sn = "irshift_slot";
        break;

    case invert_slot:
        sn = "invert_slot";
        break;

    case call_slot:
        sn = "call_slot";
        break;

    case getitem_slot:
        sn = "getitem_slot";
        break;

    case setitem_slot:
        sn = "setitem_slot";
        break;

    case delitem_slot:
        sn = "delitem_slot";
        break;

    case lt_slot:
        sn = "lt_slot";
        break;

    case le_slot:
        sn = "le_slot";
        break;

    case eq_slot:
        sn = "eq_slot";
        break;

    case ne_slot:
        sn = "ne_slot";
        break;

    case gt_slot:
        sn = "gt_slot";
        break;

    case ge_slot:
        sn = "ge_slot";
        break;

    case cmp_slot:
        sn = "cmp_slot";
        break;

    case bool_slot:
        sn = "bool_slot";
        break;

    case neg_slot:
        sn = "neg_slot";
        break;

    case pos_slot:
        sn = "pos_slot";
        break;

    case abs_slot:
        sn = "abs_slot";
        break;

    case repr_slot:
        sn = "repr_slot";
        break;

    case hash_slot:
        sn = "hash_slot";
        break;

    case index_slot:
        sn = "index_slot";
        break;

    case iter_slot:
        sn = "iter_slot";
        break;

    case next_slot:
        sn = "next_slot";
        break;

    case setattr_slot:
        sn = "setattr_slot";
        break;

    case matmul_slot:
        sn = "matmul_slot";
        break;

    case imatmul_slot:
        sn = "imatmul_slot";
        break;

    case await_slot:
        sn = "await_slot";
        break;

    case aiter_slot:
        sn = "aiter_slot";
        break;

    case anext_slot:
        sn = "anext_slot";
        break;

    default:
        sn = NULL;
    }

    return sn;
}


/*
 * Generate the initialisation function or cast operators for the type.
 */
static int generateTypeInit(classDef *cd, moduleDef *mod, FILE *fp)
{
    ctorDef *ct;
    int need_self, need_owner;

    /*
     * See if we need to name the self and owner arguments so that we can avoid
     * a compiler warning about an unused argument.
     */
    need_self = (generating_c || hasShadow(cd));
    need_owner = generating_c;

    for (ct = cd->ctors; ct != NULL; ct = ct->next)
    {
        if (usedInCode(ct->methodcode, "sipSelf"))
            need_self = TRUE;

        if (isResultTransferredCtor(ct))
            need_owner = TRUE;
        else
        {
            int a;

            for (a = 0; a < ct->pysig.nrArgs; ++a)
            {
                argDef *ad = &ct->pysig.args[a];

                if (!isInArg(ad))
                    continue;

                if (keepReference(ad))
                    need_self = TRUE;

                if (isTransferred(ad))
                    need_self = TRUE;

                if (isThisTransferred(ad))
                    need_owner = TRUE;
            }
        }
    }

    prcode(fp,
"\n"
"\n"
        );

    if (!generating_c)
        prcode(fp,
"extern \"C\" {static void *init_type_%L(sipSimpleWrapper *, PyObject *, PyObject *, PyObject **, PyObject **, PyObject **);}\n"
            , cd->iff);

    prcode(fp,
"static void *init_type_%L(sipSimpleWrapper *%s, PyObject *sipArgs, PyObject *sipKwds, PyObject **sipUnused, PyObject **%s, PyObject **sipParseErr)\n"
"{\n"
        , cd->iff, (need_self ? "sipSelf" : ""), (need_owner ? "sipOwner" : ""));

    if (hasShadow(cd))
        prcode(fp,
"    sip%C *sipCpp = SIP_NULLPTR;\n"
            ,classFQCName(cd));
    else
        prcode(fp,
"    %U *sipCpp = SIP_NULLPTR;\n"
            ,cd);

    if (tracing)
        prcode(fp,
"\n"
"    sipTrace(SIP_TRACE_INITS, \"init_type_%L()\\n\");\n"
            , cd->iff);

    /*
     * Generate the code that parses the Python arguments and calls the correct
     * constructor.
     */
    for (ct = cd->ctors; ct != NULL; ct = ct->next)
    {
        int error_flag, old_error_flag;

        if (isPrivateCtor(ct))
            continue;

        prcode(fp,
"\n"
"    {\n"
            );

        if (ct->methodcode != NULL)
        {
            error_flag = needErrorFlag(ct->methodcode);
            old_error_flag = needOldErrorFlag(ct->methodcode);
        }
        else
        {
            error_flag = old_error_flag = FALSE;
        }

        if (generateArgParser(mod, &ct->pysig, cd, NULL, ct, NULL, fp) < 0)
            return -1;

        generateConstructorCall(cd, ct, error_flag, old_error_flag, mod, fp);

        prcode(fp,
"    }\n"
            );
    }

    prcode(fp,
"\n"
"    return SIP_NULLPTR;\n"
"}\n"
        );

    return 0;
}


/*
 * Count the number of virtual members in a class.
 */
static int countVirtuals(classDef *cd)
{
    int nrvirts;
    virtOverDef *vod;
 
    nrvirts = 0;
 
    for (vod = cd->vmembers; vod != NULL; vod = vod->next)
    {
        overDef *od = vod->od;
        virtOverDef *dvod;

        if (isPrivate(od))
            continue;

        /*
         * Check we haven't already handled this C++ signature.  The same C++
         * signature should only appear more than once for overloads that are
         * enabled for different APIs and that differ in their /In/ and/or
         * /Out/ annotations.
         */
        for (dvod = cd->vmembers; dvod != vod; dvod = dvod->next)
            if (strcmp(dvod->od->cppname, od->cppname) == 0 && sameSignature(dvod->od->cppsig, od->cppsig, TRUE))
                break;

        if (dvod == vod)
            ++nrvirts;
    }
 
    return nrvirts;
}

 
/*
 * Generate the try block for a call.
 */
static void generateTry(throwArgs *ta,FILE *fp)
{
    /*
     * Generate the block if there was no throw specifier, or a non-empty
     * throw specifier.
     */
    if (exceptions && (ta == NULL || ta->nrArgs > 0))
        prcode(fp,
"            try\n"
"            {\n"
            );
}


/*
 * Generate the catch blocks for a call.
 */
static void generateCatch(throwArgs *ta, signatureDef *sd, moduleDef *mod,
        FILE *fp, int rgil)
{
    /*
     * Generate the block if there was no throw specifier, or a non-empty
     * throw specifier.
     */
    if (exceptions && (ta == NULL || ta->nrArgs > 0))
    {
        int use_handler = (abiVersion >= ABI_13_1 || (abiVersion >= ABI_12_9 && abiVersion < ABI_13_0));

        prcode(fp,
"            }\n"
            );

        if (!use_handler)
        {
            if (ta != NULL)
            {
                int a;

                for (a = 0; a < ta->nrArgs; ++a)
                    generateCatchBlock(mod, ta->args[a], sd, fp, rgil);
            }
            else if (mod->defexception != NULL)
            {
                generateCatchBlock(mod, mod->defexception, sd, fp, rgil);
            }
        }

        prcode(fp,
"            catch (...)\n"
"            {\n"
            );

        if (rgil)
            prcode(fp,
"                Py_BLOCK_THREADS\n"
"\n"
                );

        deleteOuts(mod, sd, fp);
        deleteTemps(mod, sd, fp);

        if (use_handler)
            prcode(fp,
"                void *sipExcState = SIP_NULLPTR;\n"
"                sipExceptionHandler sipExcHandler;\n"
"                std::exception_ptr sipExcPtr = std::current_exception();\n"
"\n"
"                while ((sipExcHandler = sipNextExceptionHandler(&sipExcState)) != SIP_NULLPTR)\n"
"                    if (sipExcHandler(sipExcPtr))\n"
"                        return SIP_NULLPTR;\n"
"\n"
                );

        prcode(fp,
"                sipRaiseUnknownException();\n"
"                return SIP_NULLPTR;\n"
"            }\n"
            );
    }
}


/*
 * Generate a single catch block.
 */
static void generateCatchBlock(moduleDef *mod, exceptionDef *xd,
        signatureDef *sd, FILE *fp, int rgil)
{
    scopedNameDef *ename = xd->iff->fqcname;

    /*
     * The global scope is stripped from the exception name to be consistent
     * with older versions of SIP.
     */
    prcode(fp,
"            catch (%V &%s)\n"
"            {\n"
        ,ename,(xd->cd != NULL || usedInCode(xd->raisecode, "sipExceptionRef")) ? "sipExceptionRef" : "");

    if (rgil)
        prcode(fp,
"\n"
"                Py_BLOCK_THREADS\n"
            );

    if (sd != NULL)
    {
        deleteOuts(mod, sd, fp);
        deleteTemps(mod, sd, fp);
    }

    /* See if the exception is a wrapped class. */
    if (xd->cd != NULL)
        prcode(fp,
"                /* Hope that there is a valid copy ctor. */\n"
"                %S *sipExceptionCopy = new %S(sipExceptionRef);\n"
"\n"
"                sipRaiseTypeException(sipType_%C, sipExceptionCopy);\n"
            , ename, ename
            , ename);
    else
        generateCppCodeBlock(xd->raisecode, fp);

    prcode(fp,
"\n"
"                return %s;\n"
"            }\n"
        , (sd != NULL ? "SIP_NULLPTR" : "true"));
}


/*
 * Generate a throw specifier.
 */
static void generateThrowSpecifier(throwArgs *ta,FILE *fp)
{
    if (exceptions && ta != NULL && ta->nrArgs == 0)
        prcode(fp, " noexcept");
}


/*
 * Generate a single constructor call.
 */
static void generateConstructorCall(classDef *cd, ctorDef *ct, int error_flag,
        int old_error_flag, moduleDef *mod, FILE *fp)
{
    int a;

    prcode(fp,
"        {\n"
        );

    if (ct->premethodcode != NULL)
    {
        prcode(fp, "\n");
        generateCppCodeBlock(ct->premethodcode,fp);
        prcode(fp, "\n");
    }

    if (error_flag)
        prcode(fp,
"            sipErrorState sipError = sipErrorNone;\n"
"\n"
            );
    else if (old_error_flag)
        prcode(fp,
"            int sipIsErr = 0;\n"
"\n"
            );

    if (isDeprecatedCtor(ct))
        /* Note that any temporaries will leak if an exception is raised. */
        prcode(fp,
"            if (sipDeprecated(%N, SIP_NULLPTR) < 0)\n"
"                return SIP_NULLPTR;\n"
"\n"
            , cd->pyname);

    /* Call any pre-hook. */
    if (ct->prehook != NULL)
        prcode(fp,
"            sipCallHook(\"%s\");\n"
"\n"
            ,ct->prehook);

    if (ct->methodcode != NULL)
    {
        generateCppCodeBlock(ct->methodcode,fp);
    }
    else if (generating_c)
    {
        prcode(fp,
"            sipCpp = sipMalloc(sizeof (%U));\n"
            , cd);
    }
    else
    {
        int rgil = ((release_gil || isReleaseGILCtor(ct)) && !isHoldGILCtor(ct));

        if (raisesPyExceptionCtor(ct))
            prcode(fp,
"            PyErr_Clear();\n"
"\n"
                );

        if (rgil)
            prcode(fp,
"            Py_BEGIN_ALLOW_THREADS\n"
                );

        generateTry(ct->exceptions,fp);

        if (hasShadow(cd))
            prcode(fp,
"            sipCpp = new sip%C(",classFQCName(cd));
        else
            prcode(fp,
"            sipCpp = new %U(",cd);

        if (isCastCtor(ct))
        {
            classDef *ocd;

            /* We have to fiddle the type to generate the correct code. */
            ocd = ct->pysig.args[0].u.cd;
            ct->pysig.args[0].u.cd = cd;
            prcode(fp, "a0->operator %B()", &ct->pysig.args[0]);
            ct->pysig.args[0].u.cd = ocd;
        }
        else
            generateCallArgs(mod, ct->cppsig, &ct->pysig, fp);

        prcode(fp,");\n"
            );

        generateCatch(ct->exceptions, &ct->pysig, mod, fp, rgil);

        if (rgil)
            prcode(fp,
"            Py_END_ALLOW_THREADS\n"
                );

        /*
         * This is a bit of a hack to say we want the result transferred.  We
         * don't simply call sipTransferTo() because the wrapper object hasn't
         * been fully initialised yet.
         */
        if (isResultTransferredCtor(ct))
            prcode(fp,
"\n"
"            *sipOwner = Py_None;\n"
                );
    }

    /* Handle any /KeepReference/ arguments. */
    for (a = 0; a < ct->pysig.nrArgs; ++a)
    {
        argDef *ad = &ct->pysig.args[a];

        if (!isInArg(ad))
            continue;

        if (keepReference(ad))
        {
            prcode(fp,
"\n"
"            sipKeepReference((PyObject *)sipSelf, %d, %a%s);\n"
                , ad->key, mod, ad, a, (((ad->atype == ascii_string_type || ad->atype == latin1_string_type || ad->atype == utf8_string_type) && ad->nrderefs == 1) || !isGetWrapper(ad) ? "Keep" : "Wrapper"));
        }
    }

    gc_ellipsis(&ct->pysig, fp);

    deleteTemps(mod, &ct->pysig, fp);

    prcode(fp,
"\n"
        );

    if (raisesPyExceptionCtor(ct))
    {
        prcode(fp,
"            if (PyErr_Occurred())\n"
"            {\n"
"                delete sipCpp;\n"
"                return SIP_NULLPTR;\n"
"            }\n"
"\n"
                );
    }

    if (error_flag)
    {
        prcode(fp,
"            if (sipError == sipErrorNone)\n"
            );

        if (hasShadow(cd) || ct->posthook != NULL)
            prcode(fp,
"            {\n"
                );

        if (hasShadow(cd))
            prcode(fp,
"                sipCpp->sipPySelf = sipSelf;\n"
"\n"
                );

        /* Call any post-hook. */
        if (ct->posthook != NULL)
            prcode(fp,
"            sipCallHook(\"%s\");\n"
"\n"
                , ct->posthook);

        prcode(fp,
"                return sipCpp;\n"
            );

        if (hasShadow(cd) || ct->posthook != NULL)
            prcode(fp,
"            }\n"
                );

        prcode(fp,
"\n"
"            if (sipUnused)\n"
"            {\n"
"                Py_XDECREF(*sipUnused);\n"
"            }\n"
"\n"
"            sipAddException(sipError, sipParseErr);\n"
"\n"
"            if (sipError == sipErrorFail)\n"
"                return SIP_NULLPTR;\n"
            );
    }
    else
    {
        if (old_error_flag)
        {
            prcode(fp,
"            if (sipIsErr)\n"
"            {\n"
"                if (sipUnused)\n"
"                {\n"
"                    Py_XDECREF(*sipUnused);\n"
"                }\n"
"\n"
"                sipAddException(sipErrorFail, sipParseErr);\n"
"                return SIP_NULLPTR;\n"
"            }\n"
"\n"
                );
        }

        if (hasShadow(cd))
            prcode(fp,
"            sipCpp->sipPySelf = sipSelf;\n"
"\n"
                );

        /* Call any post-hook. */
        if (ct->posthook != NULL)
            prcode(fp,
"            sipCallHook(\"%s\");\n"
"\n"
                , ct->posthook);

        prcode(fp,
"            return sipCpp;\n"
            );
    }

    prcode(fp,
"        }\n"
        );
}


/*
 * See if a member overload should be skipped.
 */
static int skipOverload(overDef *od,memberDef *md,classDef *cd,classDef *ccd,
            int want_local)
{
    /* Skip if it's not the right name. */
    if (od->common != md)
        return TRUE;

    /* Skip if it's a signal. */
    if (isSignal(od))
        return TRUE;

    /* Skip if it's a private abstract. */
    if (isAbstract(od) && isPrivate(od))
        return TRUE;

    /*
     * If we are disallowing them, skip if it's not in the current class unless
     * it is protected.
     */
    if (want_local && !isProtected(od) && ccd != cd)
        return TRUE;

    return FALSE;
}


/*
 * Generate a class member function.
 */
static int generateFunction(sipSpec *pt, memberDef *md, overDef *overs,
        classDef *cd, classDef *ocd, moduleDef *mod, FILE *fp)
{
    overDef *od;
    int need_method, need_self, need_args, need_selfarg, need_orig_self;

    /*
     * Check that there is at least one overload that needs to be handled.  See
     * if we can avoid naming the "self" argument (and suppress a compiler
     * warning).  See if we need to remember if "self" was explicitly passed as
     * an argument.  See if we need to handle keyword arguments.
     */
    need_method = need_self = need_args = need_selfarg = need_orig_self = FALSE;

    for (od = overs; od != NULL; od = od->next)
    {
        /* Skip protected methods if we don't have the means to handle them. */
        if (isProtected(od) && !hasShadow(cd))
            continue;

        if (!skipOverload(od,md,cd,ocd,TRUE))
        {
            need_method = TRUE;

            if (!isPrivate(od))
            {
                need_args = TRUE;

                if (abiVersion >= ABI_13_0 || !isStatic(od))
                {
                    need_self = TRUE;

                    if (isAbstract(od))
                        need_orig_self = TRUE;
                    else if (isVirtual(od) || isVirtualReimp(od) || usedInCode(od->methodcode, "sipSelfWasArg"))
                        need_selfarg = TRUE;
                }
            }
        }
    }

    if (need_method)
    {
        const char *pname = md->pyname->text;
        int has_auto_docstring;

        prcode(fp,
"\n"
"\n"
            );

        /* Generate the docstrings. */
        if (hasMemberDocstring(pt, overs, md))
        {
            prcode(fp,
"PyDoc_STRVAR(doc_%L_%s, \"" , cd->iff, pname);

            has_auto_docstring = generateMemberDocstring(pt, overs, md,
                    !isHiddenNamespace(cd), fp);

            prcode(fp, "\");\n"
"\n"
                );
        }
        else
        {
            has_auto_docstring = FALSE;
        }

        if (!generating_c)
            prcode(fp,
"extern \"C\" {static PyObject *meth_%L_%s(PyObject *, PyObject *%s);}\n"
            , cd->iff, pname, (noArgParser(md) || useKeywordArgs(md) ? ", PyObject *" : ""));

        prcode(fp,
"static PyObject *meth_%L_%s(PyObject *%s, PyObject *%s%s)\n"
"{\n"
            , cd->iff, pname, (need_self ? "sipSelf" : ""), (need_args ? "sipArgs" : ""), (noArgParser(md) || useKeywordArgs(md) ? ", PyObject *sipKwds" : ""));

        if (tracing)
            prcode(fp,
"    sipTrace(SIP_TRACE_METHODS, \"meth_%L_%s()\\n\");\n"
"\n"
                , cd->iff, pname);

        if (!noArgParser(md))
        {
            if (need_args)
                prcode(fp,
"    PyObject *sipParseErr = SIP_NULLPTR;\n"
                    );

            if (need_selfarg)
            {
                /*
                 * This determines if we call the explicitly scoped version or
                 * the unscoped version (which will then go via the vtable).
                 *
                 * - If the call was unbound and self was passed as the first
                 *   argument (ie. Foo.meth(self)) then we always want to call
                 *   the explicitly scoped version.
                 *
                 * - If the call was bound then we only call the unscoped
                 *   version in case there is a C++ sub-class reimplementation
                 *   that Python knows nothing about.  Otherwise, if the call
                 *   was invoked by super() within a Python reimplementation
                 *   then the Python reimplementation would be called
                 *   recursively.
                 *
                 * In addition, if the type is a derived class then we know
                 * that there can't be a C++ sub-class that we don't know
                 * about so we can avoid the vtable.
                 *
                 * Note that we would like to rename 'sipSelfWasArg' to
                 * 'sipExplicitScope' but it is part of the public API.
                 */
                if (abiVersion >= ABI_13_0)
                    prcode(fp,
"    bool sipSelfWasArg = (!PyObject_TypeCheck(sipSelf, sipTypeAsPyTypeObject(sipType_%L)) || sipIsDerivedClass((sipSimpleWrapper *)sipSelf));\n"
                        , cd->iff);
                else
                    prcode(fp,
"    bool sipSelfWasArg = (!sipSelf || sipIsDerivedClass((sipSimpleWrapper *)sipSelf));\n"
                        );
            }

            if (need_orig_self)
            {
                /*
                 * This is similar to the above but for abstract methods.  We
                 * allow the (potential) recursion because it means that the
                 * concrete implementation can be put in a mixin and it will
                 * all work.
                 */
                prcode(fp,
"    PyObject *sipOrigSelf = sipSelf;\n"
                    );
            }
        }

        for (od = overs; od != NULL; od = od->next)
        {
            /* If we are handling one variant then we must handle them all. */
            if (skipOverload(od, md, cd, ocd, FALSE))
                continue;

            if (isPrivate(od))
                continue;

            if (noArgParser(md))
            {
                generateCppCodeBlock(od->methodcode, fp);
                break;
            }

            if (generateFunctionBody(od, cd, NULL, ocd, TRUE, mod, fp) < 0)
                return -1;
        }

        if (!noArgParser(md))
        {
            prcode(fp,
"\n"
"    sipNoMethod(%s, %N, %N, ", (need_args ? "sipParseErr" : "SIP_NULLPTR"), cd->pyname, md->pyname);

            if (has_auto_docstring)
                prcode(fp, "doc_%L_%s", cd->iff, pname);
            else
                prcode(fp, "SIP_NULLPTR");

            prcode(fp, ");\n"
"\n"
"    return SIP_NULLPTR;\n"
                );
        }

        prcode(fp,
"}\n"
            );
    }

    return 0;
}


/*
 * Generate the function calls for a particular overload.
 */
static int generateFunctionBody(overDef *od, classDef *c_scope,
        mappedTypeDef *mt_scope, classDef *ocd, int deref, moduleDef *mod,
        FILE *fp)
{
    signatureDef saved;
    ifaceFileDef *o_scope;

    if (mt_scope != NULL)
        o_scope = mt_scope->iff;
    else if (ocd != NULL)
        o_scope = ocd->iff;
    else
        o_scope = NULL;

    prcode(fp,
"\n"
"    {\n"
        );

    /* In case we have to fiddle with it. */
    saved = od->pysig;

    if (isNumberSlot(od->common))
    {
        /*
         * Number slots must have two arguments because we parse them slightly
         * differently.
         */
        if (od->pysig.nrArgs == 1)
        {
            od->pysig.nrArgs = 2;
            od->pysig.args[1] = od->pysig.args[0];

            /* Insert self in the right place. */
            od->pysig.args[0].atype = class_type;
            od->pysig.args[0].name = NULL;
            od->pysig.args[0].argflags = ARG_IS_REF|ARG_IN;
            od->pysig.args[0].nrderefs = 0;
            od->pysig.args[0].defval = NULL;
            od->pysig.args[0].original_type = NULL;
            od->pysig.args[0].u.cd = ocd;
        }

        if (generateArgParser(mod, &od->pysig, c_scope, mt_scope, NULL, od, fp) < 0)
            return -1;
    }
    else if (!isIntArgSlot(od->common) && !isZeroArgSlot(od->common))
    {
        if (generateArgParser(mod, &od->pysig, c_scope, mt_scope, NULL, od, fp) < 0)
            return -1;
    }

    generateFunctionCall(c_scope, mt_scope, o_scope, od, deref, mod, fp);

    prcode(fp,
"    }\n"
        );

    od->pysig = saved;

    return 0;
}


/*
 * Generate the code to handle the result of a call to a member function.
 */
static void generateHandleResult(moduleDef *mod, overDef *od, int isNew,
        int result_size, char *prefix, FILE *fp)
{
    const char *vname;
    int a, nrvals, only, has_owner;
    argDef *res, *ad;

    res = &od->pysig.result;

    if (res->atype == void_type && res->nrderefs == 0)
        res = NULL;

    /* See if we are returning 0, 1 or more values. */
    nrvals = 0;

    if (res != NULL)
    {
        only = -1;
        ++nrvals;
    }

    has_owner = FALSE;

    for (a = 0; a < od->pysig.nrArgs; ++a)
    {
        if (isOutArg(&od->pysig.args[a]))
        {
            only = a;
            ++nrvals;
        }

        if (isThisTransferred(&od->pysig.args[a]))
            has_owner = TRUE;
    }

    /* Handle the trivial case. */
    if (nrvals == 0)
    {
        prcode(fp,
"            Py_INCREF(Py_None);\n"
"            %s Py_None;\n"
            ,prefix);

        return;
    }

    /* Handle results that are classes or mapped types separately. */
    if (res != NULL)
    {
        ifaceFileDef *iff;

        if (res->atype == mapped_type)
            iff = res->u.mtd->iff;
        else if (res->atype == class_type)
            iff = res->u.cd->iff;
        else
            iff = NULL;

        if (iff != NULL)
        {
            if (isNew || isFactory(od))
            {
                prcode(fp,
"            %s sipConvertFromNewType(",(nrvals == 1 ? prefix : "PyObject *sipResObj ="));

                if (isConstArg(res))
                    prcode(fp,"const_cast<%b *>(sipRes)",res);
                else
                    prcode(fp,"sipRes");

                prcode(fp,", sipType_%C, %s);\n"
                    , iff->fqcname, ((has_owner && isFactory(od)) ? "(PyObject *)sipOwner" : resultOwner(od)));

                /*
                 * Shortcut if this is the only value returned.
                 */
                if (nrvals == 1)
                    return;
            }
            else
            {
                int need_xfer = (isResultTransferred(od) && isStatic(od));

                prcode(fp,
"            %s sipConvertFromType(", (nrvals > 1 || need_xfer ? "PyObject *sipResObj =" : prefix));

                if (isConstArg(res))
                    prcode(fp,"const_cast<%b *>(sipRes)",res);
                else
                    prcode(fp,"sipRes");

                prcode(fp, ", sipType_%C, %s);\n"
                    , iff->fqcname, (need_xfer ? "SIP_NULLPTR" : resultOwner(od)));

                /*
                 * Transferring the result of a static overload needs an
                 * explicit call to sipTransferTo().
                 */
                if (need_xfer)
                    prcode(fp,
"\n"
"           sipTransferTo(sipResObj, Py_None);\n"
                        );

                /*
                 * Shortcut if this is the only value returned.
                 */
                if (nrvals == 1)
                {
                    if (need_xfer)
                        prcode(fp,
"\n"
"           return sipResObj;\n"
                        );

                    return;
                }
            }
        }
    }

    /* If there are multiple values then build a tuple. */
    if (nrvals > 1)
    {
        prcode(fp,
"            %s sipBuildResult(0, \"(",prefix);

        /* Build the format string. */
        if (res != NULL)
            prcode(fp, "%s", ((res->atype == mapped_type || res->atype == class_type) ? "R" : getBuildResultFormat(res)));

        for (a = 0; a < od->pysig.nrArgs; ++a)
        {
            argDef *ad = &od->pysig.args[a];

            if (isOutArg(ad))
                prcode(fp, "%s", getBuildResultFormat(ad));
        }

        prcode(fp,")\"");

        /* Pass the values for conversion. */
        if (res != NULL)
        {
            prcode(fp, ", sipRes");

            if (res->atype == mapped_type || res->atype == class_type)
                prcode(fp, "Obj");
            else if (res->atype == enum_type && res->u.ed->fqcname != NULL)
                prcode(fp, ", sipType_%C", res->u.ed->fqcname);
        }

        for (a = 0; a < od->pysig.nrArgs; ++a)
        {
            argDef *ad = &od->pysig.args[a];

            if (isOutArg(ad))
            {
                prcode(fp, ", %a", mod, ad, a);

                if (ad->atype == mapped_type)
                    prcode(fp, ", sipType_%T, %s", ad, (isTransferredBack(ad) ? "Py_None" : "SIP_NULLPTR"));
                else if (ad->atype == class_type)
                    prcode(fp, ", sipType_%C, %s", classFQCName(ad->u.cd), (isTransferredBack(ad) ? "Py_None" : "SIP_NULLPTR"));
                else if (ad->atype == enum_type && ad->u.ed->fqcname != NULL)
                    prcode(fp,", sipType_%C", ad->u.ed->fqcname);
            }
        }

        prcode(fp,");\n"
            );

        /* All done for multiple values. */
        return;
    }

    /* Deal with the only returned value. */
    if (only < 0)
    {
        ad = res;
        vname = "sipRes";
    }
    else
    {
        ad = &od->pysig.args[only];
        vname = get_argument_name(ad, only, mod);
    }

    switch (ad->atype)
    {
    case mapped_type:
    case class_type:
        {
            int needNew = needNewInstance(ad);
            ifaceFileDef *iff;

            if (ad->atype == mapped_type)
                iff = ad->u.mtd->iff;
            else
                iff = ad->u.cd->iff;

            prcode(fp,
"            %s sipConvertFrom%sType(", prefix, (needNew ? "New" : ""));

            if (isConstArg(ad))
                prcode(fp,"const_cast<%b *>(%s)",ad,vname);
            else
                prcode(fp,"%s",vname);

            prcode(fp, ", sipType_%C, ", iff->fqcname);

            if (needNew || !isTransferredBack(ad))
                prcode(fp, "SIP_NULLPTR);\n");
            else
                prcode(fp, "Py_None);\n");
        }

        break;

    case bool_type:
    case cbool_type:
        prcode(fp,
"            %s PyBool_FromLong(%s);\n"
            ,prefix,vname);

        break;

    case ascii_string_type:
        if (ad->nrderefs == 0)
            prcode(fp,
"            %s PyUnicode_DecodeASCII(&%s, 1, SIP_NULLPTR);\n"
                , prefix, vname);
        else
            prcode(fp,
"            if (%s == SIP_NULLPTR)\n"
"            {\n"
"                Py_INCREF(Py_None);\n"
"                return Py_None;\n"
"            }\n"
"\n"
"            %s PyUnicode_DecodeASCII(%s, strlen(%s), SIP_NULLPTR);\n"
            , vname
            , prefix, vname, vname);

        break;

    case latin1_string_type:
        if (ad->nrderefs == 0)
            prcode(fp,
"            %s PyUnicode_DecodeLatin1(&%s, 1, SIP_NULLPTR);\n"
                , prefix, vname);
        else
            prcode(fp,
"            if (%s == SIP_NULLPTR)\n"
"            {\n"
"                Py_INCREF(Py_None);\n"
"                return Py_None;\n"
"            }\n"
"\n"
"            %s PyUnicode_DecodeLatin1(%s, strlen(%s), SIP_NULLPTR);\n"
            , vname
            , prefix, vname, vname);

        break;

    case utf8_string_type:
        if (ad->nrderefs == 0)
            prcode(fp,
"            %s PyUnicode_FromStringAndSize(&%s, 1);\n"
                , prefix, vname);
        else
            prcode(fp,
"            if (%s == SIP_NULLPTR)\n"
"            {\n"
"                Py_INCREF(Py_None);\n"
"                return Py_None;\n"
"            }\n"
"\n"
"            %s PyUnicode_FromString(%s);\n"
            , vname
            , prefix, vname);

        break;

    case sstring_type:
    case ustring_type:
    case string_type:
        if (ad->nrderefs == 0)
            prcode(fp,
"            %s PyBytes_FromStringAndSize(%s&%s, 1);\n"
                ,prefix,(ad->atype != string_type) ? "(char *)" : "",vname);
        else
            prcode(fp,
"            if (%s == SIP_NULLPTR)\n"
"            {\n"
"                Py_INCREF(Py_None);\n"
"                return Py_None;\n"
"            }\n"
"\n"
"            %s PyBytes_FromString(%s%s);\n"
            ,vname
            ,prefix,(ad->atype != string_type) ? "(char *)" : "",vname);

        break;

    case wstring_type:
        if (ad->nrderefs == 0)
            prcode(fp,
"            %s PyUnicode_FromWideChar(&%s, 1);\n"
                , prefix, vname);
        else
            prcode(fp,
"            if (%s == SIP_NULLPTR)\n"
"            {\n"
"                Py_INCREF(Py_None);\n"
"                return Py_None;\n"
"            }\n"
"\n"
"            %s PyUnicode_FromWideChar(%s, (Py_ssize_t)wcslen(%s));\n"
            , vname
            , prefix, vname, vname);

        break;

    case enum_type:
        if (ad->u.ed->fqcname != NULL)
        {
            const char *cast_prefix, *cast_suffix;

            if (generating_c)
            {
                cast_prefix = cast_suffix = "";
            }
            else
            {
                cast_prefix = "static_cast<int>(";
                cast_suffix = ")";
            }

            prcode(fp,
"            %s sipConvertFromEnum(%s%s%s, sipType_%C);\n"
                , prefix, cast_prefix, vname, cast_suffix, ad->u.ed->fqcname);

            break;
        }

        /* Drop through. */

    case byte_type:
    case sbyte_type:
    case short_type:
    case int_type:
    case cint_type:
        prcode(fp,
"            %s PyLong_FromLong(%s);\n"
            ,prefix,vname);

        break;

    case long_type:
        prcode(fp,
"            %s PyLong_FromLong(%s);\n"
            ,prefix,vname);

        break;

    case ubyte_type:
    case ushort_type:
        prcode(fp,
"            %s PyLong_FromUnsignedLong(%s);\n"
            , prefix, vname);

        break;

    case uint_type:
    case ulong_type:
    case size_type:
        prcode(fp,
"            %s PyLong_FromUnsignedLong(%s);\n"
            , prefix, vname);

        break;

    case ssize_type:
        prcode(fp,
"            %s PyLong_FromSsize_t(%s);\n"
            , prefix, vname);

        break;

    case longlong_type:
        prcode(fp,
"            %s PyLong_FromLongLong(%s);\n"
            ,prefix,vname);

        break;

    case ulonglong_type:
        prcode(fp,
"            %s PyLong_FromUnsignedLongLong(%s);\n"
            ,prefix,vname);

        break;

    case void_type:
        {
            prcode(fp,
"            %s sipConvertFrom%sVoidPtr", prefix, (isConstArg(ad) ? "Const" : ""));

            if (result_size < 0)
            {
                prcode(fp, "(");
                generateVoidPtrCast(ad, fp);
                prcode(fp, "%s", vname);
            }
            else
            {
                prcode(fp, "AndSize(");
                generateVoidPtrCast(ad, fp);
                prcode(fp, "%s, %a", vname, mod, &od->pysig.args[result_size], result_size);
            }

            prcode(fp, ");\n"
                    );
        }

        break;

    case capsule_type:
        prcode(fp,
"            %s PyCapsule_New(%s, \"%S\", SIP_NULLPTR);\n"
            , prefix, vname, ad->u.cap);
        break;

    case struct_type:
    case union_type:
        prcode(fp,
"            %s sipConvertFrom%sVoidPtr(%s);\n"
            , prefix, (isConstArg(ad) ? "Const" : ""), vname);
        break;

    case float_type:
    case cfloat_type:
        prcode(fp,
"            %s PyFloat_FromDouble((double)%s);\n"
            ,prefix,vname);

        break;

    case double_type:
    case cdouble_type:
        prcode(fp,
"            %s PyFloat_FromDouble(%s);\n"
            ,prefix,vname);

        break;

    case pyobject_type:
    case pytuple_type:
    case pylist_type:
    case pydict_type:
    case pycallable_type:
    case pyslice_type:
    case pytype_type:
    case pybuffer_type:
    case pyenum_type:
        prcode(fp,
"            %s %s;\n"
            ,prefix,vname);

        break;

    /* Supress a compiler warning. */
    default:
        ;
    }
}


/*
 * Return the owner of a method result.
 */
static const char *resultOwner(overDef *od)
{
    if (isResultTransferredBack(od))
        return "Py_None";

    if (isResultTransferred(od))
        return "sipSelf";

    return "SIP_NULLPTR";
}


/*
 * Check if an argument is a string rather than a char type.
 */
static int isString(argDef *ad)
{
    int nrderefs = ad->nrderefs;

    if (isOutArg(ad) && !isReference(ad))
        --nrderefs;

    return nrderefs > 0;
}


/*
 * Return the format string used by sipBuildResult() for a particular type.
 */
static const char *getBuildResultFormat(argDef *ad)
{
    switch (ad->atype)
    {
    case fake_void_type:
    case mapped_type:
    case class_type:
        if (needNewInstance(ad))
            return "N";

        return "D";

    case bool_type:
    case cbool_type:
        return "b";

    case ascii_string_type:
    case latin1_string_type:
    case utf8_string_type:
        return isString(ad) ? "A" : "a";

    case sstring_type:
    case ustring_type:
    case string_type:
        return isString(ad) ? "s" : "c";

    case wstring_type:
        return isString(ad) ? "x" : "w";

    case enum_type:
        return (ad->u.ed->fqcname != NULL) ? "F" : "e";

    case byte_type:
    case sbyte_type:
        return "L";

    case ubyte_type:
        return "M";

    case short_type:
        return "h";

    case ushort_type:
        return "t";

    case int_type:
    case cint_type:
        return "i";

    case uint_type:
        return "u";

    case size_type:
        return "=";

    case long_type:
        return "l";

    case ulong_type:
        return "m";

    case longlong_type:
        return "n";

    case ulonglong_type:
        return "o";

    case struct_type:
    case union_type:
    case void_type:
        return "V";

    case capsule_type:
        return "z";

    case float_type:
    case cfloat_type:
        return "f";

    case double_type:
    case cdouble_type:
        return "d";

    case pyobject_type:
    case pytuple_type:
    case pylist_type:
    case pydict_type:
    case pycallable_type:
    case pyslice_type:
    case pytype_type:
    case pybuffer_type:
    case pyenum_type:
        return "R";

    /* Supress a compiler warning. */
    default:
        ;
    }

    /* We should never get here. */
    return "";
}


/*
 * Return TRUE if an argument (or result) needs to be copied to the heap.
 */
static int needsHeapCopy(argDef *ad, int usingCopyCtor)
{
    /* The type is a class or mapped type and not a pointer. */
    if (!noCopy(ad) && (ad->atype == class_type || ad->atype == mapped_type) && ad->nrderefs == 0)
    {
        /* We need a copy unless it is a non-const reference. */
        if (!isReference(ad) || isConstArg(ad))
        {
            /* We assume we can copy a mapped type. */
            if (ad->atype != class_type)
                return TRUE;

            /* We can't copy an abstract class. */
            if (isAbstractClass(ad->u.cd))
                return FALSE;

            /* We can copy if we have a public copy ctor. */
            if (!cannotCopy(ad->u.cd))
                return TRUE;

            /* We can't copy if we must use a copy ctor. */
            if (usingCopyCtor)
                return FALSE;

            /* We can copy if we have a public assignment operator. */
            return !cannotAssign(ad->u.cd);
        }
    }

    return FALSE;
}


/*
 * Generate a variable to hold the result of a function call if one is needed.
 */
static int generateResultVar(ifaceFileDef *scope, overDef *od, argDef *res,
        const char *indent, FILE *fp)
{
    int is_result;

    /* See if sipRes is needed. */
    is_result = (!isInplaceNumberSlot(od->common) &&
             !isInplaceSequenceSlot(od->common) &&
             (res->atype != void_type || res->nrderefs != 0));

    if (is_result)
    {
        prcode(fp, "%s", indent);

        generateNamedValueType(scope, res, "sipRes", fp);

        /*
         * The typical %MethodCode usually causes a compiler warning, so we
         * initialise the result in that case to try and suppress it.
         */
        if (od->methodcode != NULL)
        {
            prcode(fp," = ");

            generateCastZero(res, fp);
        }

        prcode(fp,";\n"
            );
    }

    return is_result;
}


/*
 * Generate a function call.
 */
static void generateFunctionCall(classDef *c_scope, mappedTypeDef *mt_scope,
        ifaceFileDef *o_scope, overDef *od, int deref, moduleDef *mod,
        FILE *fp)
{
    int needsNew, error_flag, old_error_flag, newline, is_result, result_size,
            a, deltemps, post_process, static_factory;
    const char *error_value;
    argDef *res = &od->pysig.result, orig_res;
    ifaceFileDef *scope;
    nameDef *pyname;

    if (mt_scope != NULL)
    {
        scope = mt_scope->iff;
        pyname = mt_scope->pyname;
    }
    else if (c_scope != NULL)
    {
        scope = c_scope->iff;
        pyname = c_scope->pyname;
    }
    else
    {
        scope = NULL;
        pyname = NULL;
    }

    static_factory = ((scope == NULL || isStatic(od)) && isFactory(od));

    prcode(fp,
"        {\n"
        );

    /*
     * If there is no shadow class then protected methods can never be called.
     */
    if (isProtected(od) && !hasShadow(c_scope))
    {
        prcode(fp,
"            /* Never reached. */\n"
"        }\n"
            );

        return;
    }

    /* Save the full result type as we may want to fiddle with it. */
    orig_res = *res;

    /* See if we need to make a copy of the result on the heap. */
    needsNew = needsHeapCopy(res, FALSE);

    if (needsNew)
        resetIsConstArg(res);

    is_result = newline = generateResultVar(scope, od, res, "            ",
            fp);

    result_size = -1;
    deltemps = TRUE;
    post_process = FALSE;

    /* See if we want to keep a reference to the result. */
    if (keepReference(res))
        post_process = TRUE;

    for (a = 0; a < od->pysig.nrArgs; ++a)
    {
        argDef *ad = &od->pysig.args[a];

        if (isResultSize(ad))
            result_size = a;

        if (static_factory && keepReference(ad))
            post_process = TRUE;

        /*
         * If we have an In,Out argument that has conversion code then we delay
         * the destruction of any temporary variables until after we have
         * converted the outputs.
         */
        if (isInArg(ad) && isOutArg(ad) && convertToCode(ad) != NULL)
        {
            deltemps = FALSE;
            post_process = TRUE;
        }

        /*
         * If we are returning a class via an output only reference or pointer
         * then we need an instance on the heap.
         */
        if (needNewInstance(ad))
        {
            prcode(fp,
"            %a = new %b();\n"
                , mod, ad, a, ad);

            newline = TRUE;
        }
    }

    if (post_process)
    {
        prcode(fp,
"            PyObject *sipResObj;\n"
                );

        newline = TRUE;
    }

    if (od->premethodcode != NULL)
    {
        prcode(fp, "\n");
        generateCppCodeBlock(od->premethodcode,fp);
    }

    error_flag = old_error_flag = FALSE;

    if (od->methodcode != NULL)
    {
        /* See if the handwritten code seems to be using the error flag. */
        if (needErrorFlag(od->methodcode))
        {
            prcode(fp,
"            sipErrorState sipError = sipErrorNone;\n"
                );

            newline = TRUE;
            error_flag = TRUE;
        }
        else if (needOldErrorFlag(od->methodcode))
        {
            prcode(fp,
"            int sipIsErr = 0;\n"
                );

            newline = TRUE;
            old_error_flag = TRUE;
        }
    }

    if (newline)
        prcode(fp,
"\n"
            );

    /* If it is abstract make sure that self was bound. */
    if (isAbstract(od))
        prcode(fp,
"            if (!sipOrigSelf)\n"
"            {\n"
"                sipAbstractMethod(%N, %N);\n"
"                return SIP_NULLPTR;\n"
"            }\n"
"\n"
            , c_scope->pyname, od->common->pyname);

    if (isDeprecated(od))
    {
        /* Note that any temporaries will leak if an exception is raised. */
        if (pyname != NULL)
            prcode(fp,
"            if (sipDeprecated(%N, %N) < 0)\n"
                , pyname, od->common->pyname);
        else
            prcode(fp,
"            if (sipDeprecated(SIP_NULLPTR, %N) < 0)\n"
                , od->common->pyname);

        prcode(fp,
"                return %s;\n"
"\n"
            , ((isVoidReturnSlot(od->common) || isIntReturnSlot(od->common) || isSSizeReturnSlot(od->common) || isHashReturnSlot(od->common)) ? "-1" : "SIP_NULLPTR"));
    }

    /* Call any pre-hook. */
    if (od->prehook != NULL)
        prcode(fp,
"            sipCallHook(\"%s\");\n"
"\n"
            ,od->prehook);

    if (od->methodcode != NULL)
        generateCppCodeBlock(od->methodcode,fp);
    else
    {
        int rgil = ((release_gil || isReleaseGIL(od)) && !isHoldGIL(od));
        int closing_paren = FALSE;

        if (needsNew && generating_c)
        {
            prcode(fp,
"            if ((sipRes = (%b *)sipMalloc(sizeof (%b))) == SIP_NULLPTR)\n"
"        {\n"
                ,res,res);

            gc_ellipsis(&od->pysig, fp);

            prcode(fp,
"                return SIP_NULLPTR;\n"
"            }\n"
"\n"
                );
        }

        if (raisesPyException(od))
            prcode(fp,
"            PyErr_Clear();\n"
"\n"
                );

        if (c_scope != NULL && c_scope->len_cpp_name != NULL)
            generateSequenceSupport(c_scope, od, mod, fp);

        if (rgil)
            prcode(fp,
"            Py_BEGIN_ALLOW_THREADS\n"
                );

        generateTry(od->exceptions,fp);

        prcode(fp,
"            ");

        if (od->common->slot != cmp_slot && is_result)
        {
            /* Construct a copy on the heap if needed. */
            if (needsNew)
            {
                if (generating_c)
                {
                    prcode(fp,"*sipRes = ");
                }
                else if (res->atype == class_type && cannotCopy(res->u.cd))
                {
                    prcode(fp, "sipRes = reinterpret_cast<%b *>(::operator new(sizeof (%b)));\n"
"            *sipRes = ", res, res);
                }
                else
                {
                    prcode(fp,"sipRes = new %b(",res);
                    closing_paren = TRUE;
                }
            }
            else
            {
                prcode(fp,"sipRes = ");

                /*
                 * See if we need the address of the result.  Any reference
                 * will be non-const.
                 */
                if ((res->atype == class_type || res->atype == mapped_type) && (res->nrderefs == 0 || isReference(res)))
                    prcode(fp,"&");
            }
        }

        switch (od->common->slot)
        {
        case no_slot:
            generateCppFunctionCall(mod, scope, o_scope, od, fp);
            break;

        case getitem_slot:
            prcode(fp, "(*sipCpp)[");
            generateSlotArg(mod, &od->pysig, 0, fp);
            prcode(fp,"]");
            break;

        case call_slot:
            prcode(fp, "(*sipCpp)(");
            generateCallArgs(mod, od->cppsig, &od->pysig, fp);
            prcode(fp,")");
            break;

        case int_slot:
        case float_slot:
            prcode(fp, "*sipCpp");
            break;

        case add_slot:
            generateNumberSlotCall(mod, od, "+", fp);
            break;

        case concat_slot:
            generateBinarySlotCall(mod, scope, od, "+", deref, fp);
            break;

        case sub_slot:
            generateNumberSlotCall(mod, od, "-", fp);
            break;

        case mul_slot:
        case matmul_slot:
            generateNumberSlotCall(mod, od, "*", fp);
            break;

        case repeat_slot:
            generateBinarySlotCall(mod, scope, od, "*", deref, fp);
            break;

        case truediv_slot:
            generateNumberSlotCall(mod, od, "/", fp);
            break;

        case mod_slot:
            generateNumberSlotCall(mod, od, "%", fp);
            break;

        case and_slot:
            generateNumberSlotCall(mod, od, "&", fp);
            break;

        case or_slot:
            generateNumberSlotCall(mod, od, "|", fp);
            break;

        case xor_slot:
            generateNumberSlotCall(mod, od, "^", fp);
            break;

        case lshift_slot:
            generateNumberSlotCall(mod, od, "<<", fp);
            break;

        case rshift_slot:
            generateNumberSlotCall(mod, od, ">>", fp);
            break;

        case iadd_slot:
        case iconcat_slot:
            generateBinarySlotCall(mod, scope, od, "+=", deref, fp);
            break;

        case isub_slot:
            generateBinarySlotCall(mod, scope, od, "-=", deref, fp);
            break;

        case imul_slot:
        case irepeat_slot:
        case imatmul_slot:
            generateBinarySlotCall(mod, scope, od, "*=", deref, fp);
            break;

        case itruediv_slot:
            generateBinarySlotCall(mod, scope, od, "/=", deref, fp);
            break;

        case imod_slot:
            generateBinarySlotCall(mod, scope, od, "%=", deref, fp);
            break;

        case iand_slot:
            generateBinarySlotCall(mod, scope, od, "&=", deref, fp);
            break;

        case ior_slot:
            generateBinarySlotCall(mod, scope, od, "|=", deref, fp);
            break;

        case ixor_slot:
            generateBinarySlotCall(mod, scope, od, "^=", deref, fp);
            break;

        case ilshift_slot:
            generateBinarySlotCall(mod, scope, od, "<<=", deref, fp);
            break;

        case irshift_slot:
            generateBinarySlotCall(mod, scope, od, ">>=", deref, fp);
            break;

        case invert_slot:
            prcode(fp, "~(*sipCpp)");
            break;

        case lt_slot:
            generateComparisonSlotCall(mod, scope, od, "<", ">=", deref, fp);
            break;

        case le_slot:
            generateComparisonSlotCall(mod, scope, od, "<=", ">", deref, fp);
            break;

        case eq_slot:
            generateComparisonSlotCall(mod, scope, od, "==", "!=", deref, fp);
            break;

        case ne_slot:
            generateComparisonSlotCall(mod, scope, od, "!=", "==", deref, fp);
            break;

        case gt_slot:
            generateComparisonSlotCall(mod, scope, od, ">", "<=", deref, fp);
            break;

        case ge_slot:
            generateComparisonSlotCall(mod, scope, od, ">=", "<", deref, fp);
            break;

        case neg_slot:
            prcode(fp, "-(*sipCpp)");
            break;

        case pos_slot:
            prcode(fp, "+(*sipCpp)");
            break;

        case cmp_slot:
            prcode(fp,"if ");
            generateBinarySlotCall(mod, scope, od, "<", deref, fp);
            prcode(fp,"\n"
"                sipRes = -1;\n"
"            else if ");
            generateBinarySlotCall(mod, scope, od, ">", deref, fp);
            prcode(fp,"\n"
"                sipRes = 1;\n"
"            else\n"
"                sipRes = 0");

            break;

        /* Supress a compiler warning. */
        default:
            ;
        }

        if (closing_paren)
            prcode(fp,")");

        prcode(fp,";\n"
            );

        generateCatch(od->exceptions, &od->pysig, mod, fp, rgil);

        if (rgil)
            prcode(fp,
"            Py_END_ALLOW_THREADS\n"
                );
    }

    for (a = 0; a < od->pysig.nrArgs; ++a)
    {
        argDef *ad = &od->pysig.args[a];

        if (!isInArg(ad))
            continue;

        /* Handle any /KeepReference/ arguments except for static factories. */
        if (!static_factory && keepReference(ad))
        {
            prcode(fp,
"\n"
"            sipKeepReference(%s, %d, %a%s);\n"
                , (scope == NULL || isStatic(od) ? "SIP_NULLPTR" : "sipSelf"), ad->key, mod, ad, a, (((ad->atype == ascii_string_type || ad->atype == latin1_string_type || ad->atype == utf8_string_type) && ad->nrderefs == 1) || !isGetWrapper(ad) ? "Keep" : "Wrapper"));
        }

        /* Handle /TransferThis/ for non-factory methods. */
        if (!isFactory(od) && isThisTransferred(ad))
        {
            prcode(fp,
"\n"
"            if (sipOwner)\n"
"                sipTransferTo(sipSelf, (PyObject *)sipOwner);\n"
"            else\n"
"                sipTransferBack(sipSelf);\n"
                    );
        }
    }

    if (isThisTransferredMeth(od))
        prcode(fp,
"\n"
"            sipTransferTo(sipSelf, SIP_NULLPTR);\n"
                );

    gc_ellipsis(&od->pysig, fp);

    if (deltemps && !isZeroArgSlot(od->common))
        deleteTemps(mod, &od->pysig, fp);

    prcode(fp,
"\n"
        );

    /* Handle the error flag if it was used. */
    error_value = ((isVoidReturnSlot(od->common) || isIntReturnSlot(od->common) || isSSizeReturnSlot(od->common) || isHashReturnSlot(od->common)) ? "-1" : "0");

    if (raisesPyException(od))
    {
        prcode(fp,
"            if (PyErr_Occurred())\n"
"                return %s;\n"
"\n"
                , error_value);
    }
    else if (error_flag)
    {
        if (!isZeroArgSlot(od->common))
            prcode(fp,
"            if (sipError == sipErrorFail)\n"
"                return %s;\n"
"\n"
                , error_value);

        prcode(fp,
"            if (sipError == sipErrorNone)\n"
"            {\n"
            );
    }
    else if (old_error_flag)
    {
        prcode(fp,
"            if (sipIsErr)\n"
"                return %s;\n"
"\n"
            , error_value);
    }

    /* Call any post-hook. */
    if (od->posthook != NULL)
        prcode(fp,
"\n"
"            sipCallHook(\"%s\");\n"
            ,od->posthook);

    if (isVoidReturnSlot(od->common))
        prcode(fp,
"            return 0;\n"
            );
    else if (isInplaceNumberSlot(od->common) || isInplaceSequenceSlot(od->common))
        prcode(fp,
"            Py_INCREF(sipSelf);\n"
"            return sipSelf;\n"
            );
    else if (isIntReturnSlot(od->common) || isSSizeReturnSlot(od->common) || isHashReturnSlot(od->common))
        prcode(fp,
"            return sipRes;\n"
            );
    else
    {
        generateHandleResult(mod, od, needsNew, result_size,
                (post_process ? "sipResObj =" : "return"), fp);

        /* Delete the temporaries now if we haven't already done so. */
        if (!deltemps)
            deleteTemps(mod, &od->pysig, fp);

        /*
         * Keep a reference to a pointer to a class if it isn't owned by
         * Python.
         */
        if (keepReference(res))
            prcode(fp,
"\n"
"            sipKeepReference(%s, %d, sipResObj);\n"
                , (isStatic(od) ? "SIP_NULLPTR" : "sipSelf"), res->key);

        /*
         * Keep a reference to any argument with the result if the function is
         * a static factory.
         */
        if (static_factory)
        {
            for (a = 0; a < od->pysig.nrArgs; ++a)
            {
                argDef *ad = &od->pysig.args[a];

                if (!isInArg(ad))
                    continue;

                if (keepReference(ad))
                {
                    prcode(fp,
"\n"
"            sipKeepReference(sipResObj, %d, %a%s);\n"
                        , ad->key, mod, ad, a, (((ad->atype == ascii_string_type || ad->atype == latin1_string_type || ad->atype == utf8_string_type) && ad->nrderefs == 1) || !isGetWrapper(ad) ? "Keep" : "Wrapper"));
                }
            }
        }

        if (post_process)
            prcode(fp,
"\n"
"            return sipResObj;\n"
                );
    }

    if (error_flag)
    {
        prcode(fp,
"            }\n"
            );

        if (!isZeroArgSlot(od->common))
            prcode(fp,
"\n"
"            sipAddException(sipError, &sipParseErr);\n"
                );
    }

    prcode(fp,
"        }\n"
        );

    /* Restore the full type of the result. */
    *res = orig_res;
}


/*
 * Generate a call to a C++ function.
 */
static void generateCppFunctionCall(moduleDef *mod, ifaceFileDef *scope,
        ifaceFileDef *o_scope, overDef *od, FILE *fp)
{
    const char *mname = od->cppname;
    int parens = 1;

    /*
     * If the function is protected then call the public wrapper.  If it is
     * virtual then call the explicit scoped function if "self" was passed as
     * the first argument.
     */

    if (scope == NULL)
        prcode(fp, "%s(", mname);
    else if (scope->type == namespace_iface)
        prcode(fp, "%S::%s(", scope->fqcname, mname);
    else if (isStatic(od))
    {
        if (isProtected(od))
            prcode(fp, "sip%C::sipProtect_%s(", scope->fqcname, mname);
        else
            prcode(fp, "%S::%s(", o_scope->fqcname, mname);
    }
    else if (isProtected(od))
    {
        if (!isAbstract(od) && (isVirtual(od) || isVirtualReimp(od)))
        {
            prcode(fp, "sipCpp->sipProtectVirt_%s(sipSelfWasArg", mname);

            if (od->cppsig->nrArgs > 0)
                prcode(fp, ", ");
        }
        else
            prcode(fp, "sipCpp->sipProtect_%s(", mname);
    }
    else if (!isAbstract(od) && (isVirtual(od) || isVirtualReimp(od)))
    {
        prcode(fp, "(sipSelfWasArg ? sipCpp->%S::%s(", o_scope->fqcname, mname);
        generateCallArgs(mod, od->cppsig, &od->pysig, fp);
        prcode(fp, ") : sipCpp->%s(", mname);
        ++parens;
    }
    else
        prcode(fp, "sipCpp->%s(", mname);

    generateCallArgs(mod, od->cppsig, &od->pysig, fp);

    while (parens--)
        prcode(fp, ")");
}


/*
 * Generate argument to a slot.
 */
static void generateSlotArg(moduleDef *mod, signatureDef *sd, int argnr,
        FILE *fp)
{
    argDef *ad;
    int deref;

    ad = &sd->args[argnr];
    deref = ((ad->atype == class_type || ad->atype == mapped_type) && ad->nrderefs == 0);

    prcode(fp, "%s%a", (deref ? "*" : ""), mod, ad, argnr);
}


/*
 * Generate the call to a comparison slot method.
 */
static void generateComparisonSlotCall(moduleDef *mod, ifaceFileDef *scope,
        overDef *od, const char *op, const char *cop, int deref, FILE *fp)
{
    if (isComplementary(od))
    {
        op = cop;
        prcode(fp, "!");
    }

    if (!isGlobal(od))
    {
        const char *deref_s = (deref ? "->" : ".");

        if (isAbstract(od))
            prcode(fp, "sipCpp%soperator%s(", deref_s, op);
        else
            prcode(fp, "sipCpp%s%S::operator%s(", deref_s, scope->fqcname, op);
    }
    else
    {
        /* If it has been moved from a namespace then get the C++ scope. */
        if (od->common->ns_scope != NULL)
            prcode(fp, "%S::", od->common->ns_scope->fqcname);

        if (deref)
            prcode(fp, "operator%s((*sipCpp), ", op);
        else
            prcode(fp, "operator%s(sipCpp, ", op);
    }

    generateSlotArg(mod, &od->pysig, 0, fp);
    prcode(fp, ")");
}


/*
 * Generate the call to a binary (non-number) slot method.
 */
static void generateBinarySlotCall(moduleDef *mod, ifaceFileDef *scope,
        overDef *od, const char *op, int deref, FILE *fp)
{
    generateComparisonSlotCall(mod, scope, od, op, "", deref, fp);
}


/*
 * Generate the call to a binary number slot method.
 */
static void generateNumberSlotCall(moduleDef *mod, overDef *od, char *op,
        FILE *fp)
{
    prcode(fp, "(");
    generateSlotArg(mod, &od->pysig, 0, fp);
    prcode(fp, " %s ", op);
    generateSlotArg(mod, &od->pysig, 1, fp);
    prcode(fp, ")");
}


/*
 * Generate the argument variables for a member function/constructor/operator.
 */
static int generateArgParser(moduleDef *mod, signatureDef *sd,
        classDef *c_scope, mappedTypeDef *mt_scope, ctorDef *ct, overDef *od,
        FILE *fp)
{
    int a, optargs, arraylenarg, handle_self, single_arg, need_owner,
            ctor_needs_self;
    ifaceFileDef *scope;
    argDef *arraylenarg_ad;

    /* Suppress compiler warnings. */
    arraylenarg = 0;
    arraylenarg_ad = NULL;

    if (mt_scope != NULL)
        scope = mt_scope->iff;
    else if (c_scope != NULL)
    {
        /* If the class is just a namespace, then ignore it. */
        if (c_scope->iff->type == namespace_iface)
        {
            c_scope = NULL;
            scope = NULL;
        }
        else
            scope = c_scope->iff;
    }
    else
        scope = NULL;

    /* For ABI v13 and later static methods use self for the type object. */
    if (abiVersion >= ABI_13_0)
        handle_self = (od != NULL && od->common->slot == no_slot && c_scope != NULL);
    else
        handle_self = (od != NULL && od->common->slot == no_slot && !isStatic(od) && c_scope != NULL);

    /*
     * Generate the local variables that will hold the parsed arguments and
     * values returned via arguments.
     */
    need_owner = FALSE;
    ctor_needs_self = FALSE;

    for (a = 0; a < sd->nrArgs; ++a)
    {
        argDef *ad = &sd->args[a];

        if (isArraySize(ad))
        {
            arraylenarg_ad = ad;
            arraylenarg = a;
        }

        generateVariable(mod, scope, ad, a, fp);

        if (isThisTransferred(ad))
            need_owner = TRUE;

        if (ct != NULL && isTransferred(ad))
            ctor_needs_self = TRUE;
    }

    if (od != NULL && need_owner)
        prcode(fp,
"        sipWrapper *sipOwner = SIP_NULLPTR;\n"
            );

    if (handle_self && !isStatic(od))
    {
        const char *const_str = (isConst(od) ? "const " : "");

        if (isProtected(od) && hasShadow(c_scope))
            prcode(fp,
"        %ssip%C *sipCpp;\n"
                , const_str, classFQCName(c_scope));
        else
            prcode(fp,
"        %s%U *sipCpp;\n"
                , const_str, c_scope);

        prcode(fp,
"\n"
            );
    }
    else if (sd->nrArgs != 0)
        prcode(fp,
"\n"
            );

    /* Generate the call to the parser function. */
    single_arg = FALSE;

    if (od != NULL && isNumberSlot(od->common))
    {
        prcode(fp,
"        if (sipParsePair(&sipParseErr, sipArg0, sipArg1, \"");
    }
    else if (od != NULL && od->common->slot == setattr_slot)
    {
        /*
         * We don't even try to invoke the parser if there is a value and there
         * shouldn't be (or vice versa) so that the list of errors doesn't get
         * poluted with signatures that can never apply.
         */
        prcode(fp,
"        if (sipValue %s SIP_NULLPTR && sipParsePair(&sipParseErr, sipName, %s, \"", (isDelattr(od) ? "==" : "!="), (isDelattr(od) ? "SIP_NULLPTR" : "sipValue"));
    }
    else if ((od != NULL && useKeywordArgs(od->common)) || ct != NULL)
    {
        KwArgs kwargs;
        int is_ka_list;

        /*
         * We handle keywords if we might have been passed some (because one of
         * the overloads uses them or we are a ctor).  However this particular
         * overload might not have any.
         */
        if (od != NULL)
            kwargs = od->kwargs;
        else if (ct != NULL)
            kwargs = ct->kwargs;
        else
            kwargs = NoKwArgs;

        /*
         * The above test isn't good enough because when the flags were set in
         * the parser we couldn't know for sure if an argument was an output
         * pointer.  Therefore we check here.  The drawback is that we may
         * generate the name string for the argument but never use it, or we
         * might have an empty keyword name array or one that contains only
         * NULLs.
         */
        is_ka_list = FALSE;

        if (kwargs != NoKwArgs)
        {
            int a;

            for (a = 0; a < sd->nrArgs; ++a)
            {
                argDef *ad = &sd->args[a];

                if (isInArg(ad))
                {
                    if (!is_ka_list)
                    {
                        prcode(fp,
"        static const char *sipKwdList[] = {\n"
                            );

                        is_ka_list = TRUE;
                    }

                    if (ad->name != NULL && (kwargs == AllKwArgs || ad->defval != NULL))
                        prcode(fp,
"            %N,\n"
                            , ad->name);
                    else
                        prcode(fp,
"            SIP_NULLPTR,\n"
                            );
                }
            }

            if (is_ka_list)
                prcode(fp,
    "        };\n"
    "\n"
                    );
        }

        prcode(fp,
"        if (sipParseKwdArgs(%ssipParseErr, sipArgs, sipKwds, %s, %s, \"", (ct != NULL ? "" : "&"), (is_ka_list ? "sipKwdList" : "SIP_NULLPTR"), (ct != NULL ? "sipUnused" : "SIP_NULLPTR"));
    }
    else
    {
        single_arg = (od != NULL && od->common->slot != no_slot && !isMultiArgSlot(od->common));

        prcode(fp,
"        if (sipParseArgs(&sipParseErr, sipArg%s, \"", (single_arg ? "" : "s"));
    }

    /* Generate the format string. */
    optargs = FALSE;

    if (single_arg)
        prcode(fp, "1");

    if (ctor_needs_self)
    {
        prcode(fp, "#");
    }
    else if (handle_self)
    {
        char self_ch;

        if (isStatic(od))
            self_ch = 'C';
        else if (isReallyProtected(od))
            self_ch = 'p';
        else
            self_ch = 'B';

        prcode(fp, "%c", self_ch);
    }

    for (a = 0; a < sd->nrArgs; ++a)
    {
        char *fmt = "";
        argDef *ad = &sd->args[a];

        if (!isInArg(ad))
            continue;

        if (ad->defval != NULL && !optargs)
        {
            prcode(fp,"|");
            optargs = TRUE;
        }

        switch (ad->atype)
        {
        case ascii_string_type:
            if (isString(ad))
                fmt = "AA";
            else
                fmt = "aA";

            break;

        case latin1_string_type:
            if (isString(ad))
                fmt = "AL";
            else
                fmt = "aL";

            break;

        case utf8_string_type:
            if (isString(ad))
                fmt = "A8";
            else
                fmt = "a8";

            break;

        case sstring_type:
        case ustring_type:
        case string_type:
            if (isArray(ad))
                fmt = "k";
            else if (isString(ad))
                fmt = "s";
            else
                fmt = "c";

            break;

        case wstring_type:
            if (isArray(ad))
                fmt = "K";
            else if (isString(ad))
                fmt = "x";
            else
                fmt = "w";

            break;

        case enum_type:
            if (ad->u.ed->fqcname == NULL)
                fmt = "e";
            else if (isConstrained(ad))
                fmt = "XE";
            else
                fmt = "E";
            break;

        case bool_type:
            fmt = "b";
            break;

        case cbool_type:
            fmt = "Xb";
            break;

        case int_type:
            if (!isArraySize(ad))
                fmt = "i";

            break;

        case uint_type:
            if (!isArraySize(ad))
                fmt = "u";

            break;

        case size_type:
            if (!isArraySize(ad))
                fmt = "=";

            break;

        case cint_type:
            fmt = "Xi";
            break;

        case byte_type:
        case sbyte_type:
            if (!isArraySize(ad))
                fmt = "L";

            break;

        case ubyte_type:
            if (!isArraySize(ad))
                fmt = "M";

            break;

        case short_type:
            if (!isArraySize(ad))
                fmt = "h";

            break;

        case ushort_type:
            if (!isArraySize(ad))
                fmt = "t";

            break;

        case long_type:
            if (!isArraySize(ad))
                fmt = "l";

            break;

        case ulong_type:
            if (!isArraySize(ad))
                fmt = "m";

            break;

        case longlong_type:
            if (!isArraySize(ad))
                fmt = "n";

            break;

        case ulonglong_type:
            if (!isArraySize(ad))
                fmt = "o";

            break;

        case struct_type:
        case union_type:
        case void_type:
            fmt = "v";
            break;

        case capsule_type:
            fmt = "z";
            break;

        case float_type:
            fmt = "f";
            break;

        case cfloat_type:
            fmt = "Xf";
            break;

        case double_type:
            fmt = "d";
            break;

        case cdouble_type:
            fmt = "Xd";
            break;

        case mapped_type:
        case class_type:
            if (isArray(ad))
            {
                if (ad->nrderefs != 1 || !isInArg(ad) || isReference(ad))
                    return error("Mapped type or class with /Array/ is not a pointer\n");

                if (ad->atype == mapped_type && noRelease(ad->u.mtd))
                    return error("Mapped type does not support /Array/\n");

                if (ad->atype == class_type && !(generating_c || arrayHelper(ad->u.cd)))
                {
                    errorScopedName(classFQCName(ad->u.cd));
                    return error(" does not support /Array/\n");
                }

                if (ad->atype == class_type && abiSupportsArray())
                    fmt = ">";
                else
                    fmt = "r";
            }
            else
            {
                fmt = getSubFormatChar('J', ad);
            }

            break;

        case pyobject_type:
            fmt = getSubFormatChar('P',ad);
            break;

        case pytuple_type:
        case pylist_type:
        case pydict_type:
        case pyslice_type:
        case pytype_type:
            fmt = (isAllowNone(ad) ? "N" : "T");
            break;

        case pycallable_type:
            fmt = (isAllowNone(ad) ? "H" : "F");
            break;

        case pybuffer_type:
            fmt = (isAllowNone(ad) ? "$" : "!");
            break;

        case pyenum_type:
            fmt = (isAllowNone(ad) ? "^" : "&");
            break;

        case ellipsis_type:
            fmt = "W";
            break;

        /* Supress a compiler warning. */
        default:
            ;
        }

        /*
         * Get the wrapper if explicitly asked for or we are going to keep a
         * reference to.  However if it is an encoded string then we will get
         * the actual wrapper from the format character.
         */
        if (isGetWrapper(ad) || (keepReference(ad) && ad->atype != ascii_string_type && ad->atype != latin1_string_type && ad->atype != utf8_string_type) || (keepReference(ad) && ad->nrderefs != 1))
            prcode(fp, "@");

        prcode(fp,fmt);
    }

    prcode(fp,"\"");

    /* Generate the parameters corresponding to the format string. */

    if (ctor_needs_self)
    {
        prcode(fp,", sipSelf");
    }
    else if (handle_self)
    {
        prcode(fp,", &sipSelf");

        if (!isStatic(od))
            prcode(fp,", sipType_%C, &sipCpp", classFQCName(c_scope));
    }

    for (a = 0; a < sd->nrArgs; ++a)
    {
        argDef *ad = &sd->args[a];

        if (!isInArg(ad))
            continue;

        /* Use the wrapper name if it was explicitly asked for. */
        if (isGetWrapper(ad))
            prcode(fp, ", &%aWrapper", mod, ad, a);
        else if (keepReference(ad))
            prcode(fp, ", &%aKeep", mod, ad, a);

        switch (ad->atype)
        {
        case mapped_type:
            prcode(fp, ", sipType_%T, &%a", ad, mod, ad, a);

            if (isArray(ad))
            {
                prcode(fp, ", &%a", mod, arraylenarg_ad, arraylenarg);
            }
            else if (ad->u.mtd->convtocode != NULL && !isConstrained(ad))
            {
                if (noRelease(ad->u.mtd))
                    prcode(fp, ", SIP_NULLPTR");
                else
                    prcode(fp, ", &%aState", mod, ad, a);

                if (needsUserState(ad->u.mtd))
                    prcode(fp, ", &%aUserState", mod, ad, a);
            }

            break;

        case class_type:
            prcode(fp, ", sipType_%T, &%a", ad, mod, ad, a);

            if (isArray(ad))
            {
                prcode(fp, ", &%a", mod, arraylenarg_ad, arraylenarg);

                if (abiSupportsArray())
                    prcode(fp, ", &%aIsTemp", mod, ad, a);
            }
            else
            {
                if (isThisTransferred(ad))
                    prcode(fp, ", %ssipOwner", (ct != NULL ? "" : "&"));

                if (ad->u.cd->convtocode != NULL && !isConstrained(ad))
                {
                    prcode(fp, ", &%aState", mod, ad, a);
                }
            }

            break;

        case ascii_string_type:
            if (!keepReference(ad) && ad->nrderefs == 1)
                prcode(fp, ", &%aKeep", mod, ad, a);

            prcode(fp, ", &%a", mod, ad, a);
            break;

        case latin1_string_type:
            if (!keepReference(ad) && ad->nrderefs == 1)
                prcode(fp, ", &%aKeep", mod, ad, a);

            prcode(fp, ", &%a", mod, ad, a);
            break;

        case utf8_string_type:
            if (!keepReference(ad) && ad->nrderefs == 1)
                prcode(fp, ", &%aKeep", mod, ad, a);

            prcode(fp, ", &%a", mod, ad, a);
            break;

        case pytuple_type:
            prcode(fp, ", &PyTuple_Type, &%a", mod, ad, a);
            break;

        case pylist_type:
            prcode(fp, ", &PyList_Type, &%a", mod, ad, a);
            break;

        case pydict_type:
            prcode(fp, ", &PyDict_Type, &%a", mod, ad, a);
            break;

        case pyslice_type:
            prcode(fp, ", &PySlice_Type, &%a", mod, ad, a);
            break;

        case pytype_type:
            prcode(fp, ", &PyType_Type, &%a", mod, ad, a);
            break;

        case enum_type:
            if (ad->u.ed->fqcname != NULL)
                prcode(fp, ", sipType_%C", ad->u.ed->fqcname);

            prcode(fp, ", &%a", mod, ad, a);
            break;

        case capsule_type:
            prcode(fp, ", \"%S\", &%a", ad->u.cap, mod, ad, a);
            break;

        default:
            if (!isArraySize(ad))
                prcode(fp, ", &%a", mod, ad, a);

            if (isArray(ad))
                prcode(fp, ", &%a", mod, arraylenarg_ad, arraylenarg);
        }
    }

    prcode(fp,"))\n");

    return 0;
}


/*
 * Get the format character string for something that has sub-formats.
 */

static char *getSubFormatChar(char fc, argDef *ad)
{
    static char fmt[3];
    char flags;

    flags = 0;

    if (isTransferred(ad))
        flags |= 0x02;

    if (isTransferredBack(ad))
        flags |= 0x04;

    if (ad->atype == class_type || ad->atype == mapped_type)
    {
        if (ad->nrderefs == 0 || isDisallowNone(ad))
            flags |= 0x01;

        if (isThisTransferred(ad))
            flags |= 0x10;

        if (isConstrained(ad) || (ad->atype == class_type && ad->u.cd->convtocode == NULL))
            flags |= 0x08;
    }

    fmt[0] = fc;
    fmt[1] = '0' + flags;
    fmt[2] = '\0';

    return fmt;
}


/*
 * Return a type's %ConvertToTypeCode.
 */
static codeBlockList *convertToCode(argDef *ad)
{
    codeBlockList *convtocode;

    if (ad->atype == class_type && !isConstrained(ad))
        convtocode = ad->u.cd->convtocode;
    else if (ad->atype == mapped_type && !isConstrained(ad))
        convtocode = ad->u.mtd->convtocode;
    else
        convtocode = NULL;

    return convtocode;
}


/*
 * Garbage collect any ellipsis argument.
 */
static void gc_ellipsis(signatureDef *sd, FILE *fp)
{
    if (sd->nrArgs > 0 && sd->args[sd->nrArgs - 1].atype == ellipsis_type)
        prcode(fp,
"\n"
"            Py_DECREF(a%d);\n"
            , sd->nrArgs - 1);
}


/*
 * Delete any instances created to hold /Out/ arguments.
 */
static void deleteOuts(moduleDef *mod, signatureDef *sd, FILE *fp)
{
    int a;

    for (a = 0; a < sd->nrArgs; ++a)
    {
        argDef *ad = &sd->args[a];

        if (needNewInstance(ad))
            prcode(fp,
"                delete %a;\n"
                , mod, ad, a);
    }
}



/*
 * Delete any temporary variables on the heap created by type convertors.
 */
static void deleteTemps(moduleDef *mod, signatureDef *sd, FILE *fp)
{
    int a;

    for (a = 0; a < sd->nrArgs; ++a)
    {
        argDef *ad = &sd->args[a];
        codeBlockList *convtocode;

        if (isArray(ad) && (ad->atype == mapped_type || ad->atype == class_type))
        {
            if (!isTransferred(ad))
            {
                const char *extra_indent = "";

                if (ad->atype == class_type && abiSupportsArray())
                {
                    prcode(fp,
"            if (%aIsTemp)\n"
                        , mod, ad, a);
                    extra_indent = "    ";
                }

                if (generating_c)
                    prcode(fp,
"            %ssipFree(%a);\n"
                        , extra_indent, mod, ad, a);
                else
                    prcode(fp,
"            %sdelete[] %a;\n"
                        , extra_indent, mod, ad, a);
            }

            continue;
        }

        if (!isInArg(ad))
            continue;

        if ((ad->atype == ascii_string_type || ad->atype == latin1_string_type || ad->atype == utf8_string_type) && ad->nrderefs == 1)
        {
            prcode(fp,
"            Py_%sDECREF(%aKeep);\n"
                , (ad->defval != NULL ? "X" : ""), mod, ad, a);
        }
        else if (ad->atype == wstring_type && ad->nrderefs == 1)
        {
            if (generating_c || !isConstArg(ad))
                prcode(fp,
"            sipFree(%a);\n"
                    , mod, ad, a);
            else
                prcode(fp,
"            sipFree(const_cast<wchar_t *>(%a));\n"
                    , mod, ad, a);
        }
        else if ((convtocode = convertToCode(ad)) != NULL)
        {
            if (ad->atype == mapped_type && noRelease(ad->u.mtd))
                continue;

            prcode(fp,
"            sipReleaseType%s(", userStateSuffix(ad));

            if (generating_c || !isConstArg(ad))
                prcode(fp, "%a", mod, ad, a);
            else
                prcode(fp, "const_cast<%b *>(%a)", ad, mod, ad, a);

            prcode(fp, ", sipType_%T, %aState", ad, mod, ad, a);

            if (typeNeedsUserState(ad))
                prcode(fp, ", %aUserState", mod, ad, a);

            prcode(fp, ");\n"
                );
        }
    }
}


/*
 * Generate a list of C++ code blocks.
 */
static void generateCppCodeBlock(codeBlockList *cbl, FILE *fp)
{
    int reset_line = FALSE;

    while (cbl != NULL)
    {
        codeBlock *cb = cbl->block;

        /*
         * Fragmented fragments (possibly created when applying template types)
         * don't have a filename.
         */
        if (cb->filename != NULL)
        {
            generatePreprocLine(cb->linenr, cb->filename, fp);
            reset_line = TRUE;
        }

        prcode(fp, "%s", cb->frag);

        cbl = cbl->next;
    }

    if (reset_line)
        generatePreprocLine(currentLineNr + 1, currentFileName, fp);
}


/*
 * Generate a #line preprocessor directive.
 */
static void generatePreprocLine(int linenr, const char *fname, FILE *fp)
{
    prcode(fp,
"#line %d \"", linenr);

    while (*fname != '\0')
    {
        prcode(fp, "%c", *fname);

        if (*fname == '\\')
            prcode(fp, "\\");

        ++fname;
    }

    prcode(fp, "\"\n"
        );
}


/*
 * Create a source file.
 */
static FILE *createCompilationUnit(moduleDef *mod, stringList **generated,
        const char *fname, const char *description)
{
    FILE *fp = createFile(mod, fname, description);

    if (fp == NULL)
        return NULL;

    appendString(generated, sipStrdup(fname));

    generateCppCodeBlock(mod->unitcode, fp);

    return fp;
}


/*
 * Create a file with an optional standard header.
 */
static FILE *createFile(moduleDef *mod, const char *fname,
        const char *description)
{
    FILE *fp;

    /* Create the file. */
    if ((fp = fopen(fname, "w")) == NULL)
    {
        error("Unable to create file \"%s\"\n",fname);
        return NULL;
    }

    /* The "stack" doesn't have to be very deep. */
    previousLineNr = currentLineNr;
    currentLineNr = 1;
    previousFileName = currentFileName;
    currentFileName = fname;

    if (description != NULL)
    {
        /* Write the header. */
        prcode(fp,
"/*\n"
" * %s\n"
            , description);

        if (sipVersionStr != NULL)
            prcode(fp,
" *\n"
" * Generated by SIP %s\n"
                , sipVersionStr);

        prCopying(fp, mod, " *");

        prcode(fp,
" */\n"
            );
    }

    return fp;
}


/*
 * Generate any copying (ie. licensing) text as a comment.
 */
void prCopying(FILE *fp, moduleDef *mod, const char *comment)
{
    int needComment = TRUE;
    codeBlockList *cbl;

    if (mod->copying != NULL)
        prcode(fp, "%s\n", comment);

    for (cbl = mod->copying; cbl != NULL; cbl = cbl->next)
    {
        const char *cp;
        char buf[2];

        buf[1] = '\0';

        for (cp = cbl->block->frag; *cp != '\0'; ++cp)
        {
            if (needComment)
            {
                needComment = FALSE;
                prcode(fp, "%s ", comment);
            }

            buf[0] = *cp;
            prcode(fp, "%s", buf);

            if (*cp == '\n')
                needComment = TRUE;
        }
    }
}


/*
 * Close a file and report any errors.
 */
static int closeFile(FILE *fp)
{
    if (ferror(fp))
        return error("Error writing to \"%s\"\n", currentFileName);

    if (fclose(fp))
        return error("Error closing \"%s\"\n", currentFileName);

    currentLineNr = previousLineNr;
    currentFileName = previousFileName;

    return 0;
}


/*
 * Print formatted code.
 */
void prcode(FILE *fp, const char *fmt, ...)
{
    char ch;
    va_list ap;

    va_start(ap,fmt);

    while ((ch = *fmt++) != '\0')
        if (ch == '%')
        {
            ch = *fmt++;

            switch (ch)
            {
            case 'a':
                {
                    moduleDef *mod = va_arg(ap, moduleDef *);
                    argDef *ad = va_arg(ap, argDef *);
                    int argnr = va_arg(ap, int);

                    fprintf(fp, "%s", get_argument_name(ad, argnr, mod));
                    break;
                }

            case 'A':
                {
                    ifaceFileDef *scope = va_arg(ap, ifaceFileDef *);
                    argDef *ad = va_arg(ap, argDef *);

                    generateBaseType(scope, ad, TRUE, STRIP_NONE, fp);
                    break;
                }

            case 'b':
                {
                    argDef *ad, orig;

                    ad = va_arg(ap,argDef *);
                    orig = *ad;

                    resetIsConstArg(ad);
                    resetIsReference(ad);
                    ad->nrderefs = 0;

                    generateBaseType(NULL, ad, TRUE, STRIP_NONE, fp);

                    *ad = orig;

                    break;
                }

            case 'B':
                generateBaseType(NULL, va_arg(ap,argDef *), TRUE, STRIP_NONE,
                        fp);
                break;

            case 'c':
                {
                    char c = (char)va_arg(ap, int);

                    if (c == '\n')
                        ++currentLineNr;

                    if (isprint(c))
                        fputc(c, fp);
                    else
                        fprintf(fp, "\\%03o", (unsigned char)c);

                    break;
                }

            case 'C':
                prScopedName(fp,
                        removeGlobalScope(va_arg(ap,scopedNameDef *)), "_");
                break;

            case 'd':
                fprintf(fp,"%d",va_arg(ap,int));
                break;

            case 'D':
                {
                    /*
                     * This is the same as 'b' but never uses a typedef's name.
                     */
                    argDef *ad, orig;

                    ad = va_arg(ap,argDef *);
                    orig = *ad;

                    resetIsConstArg(ad);
                    resetIsReference(ad);
                    ad->nrderefs = 0;

                    generateBaseType(NULL, ad, FALSE, STRIP_NONE, fp);

                    *ad = orig;

                    break;
                }

            case 'E':
                {
                    enumDef *ed = va_arg(ap,enumDef *);

                    if (ed->fqcname == NULL || isProtectedEnum(ed))
                        fprintf(fp,"int");
                    else
                        prScopedName(fp, ed->fqcname, "::");

                    break;
                }

            case 'F':
                prScopedName(fp,
                        removeGlobalScope(va_arg(ap,scopedNameDef *)), "");
                break;

            case 'g':
                fprintf(fp,"%g",va_arg(ap,double));
                break;

            case 'I':
                {
                    int indent = va_arg(ap,int);

                    while (indent-- > 0)
                        fputc('\t',fp);

                    break;
                }

            case 'l':
                fprintf(fp,"%ld",va_arg(ap,long));
                break;

            case 'L':
                {
                    ifaceFileDef *iff = va_arg(ap, ifaceFileDef *);

                    prScopedName(fp, removeGlobalScope(iff->fqcname), "_");

                    break;
                }

            case 'M':
                prcode_xml = !prcode_xml;
                break;

            case 'n':
                {
                    nameDef *nd = va_arg(ap,nameDef *);

                    prCachedName(fp, nd, "sipNameNr_");
                    break;
                }

            case 'N':
                {
                    nameDef *nd = va_arg(ap,nameDef *);

                    prCachedName(fp, nd, "sipName_");
                    break;
                }

            case 'O':
                prOverloadName(fp, va_arg(ap, overDef *));
                break;

            case 's':
                {
                    const char *cp = va_arg(ap,const char *);

                    while (*cp != '\0')
                    {
                        if (*cp == '\n')
                            ++currentLineNr;

                        fputc(*cp,fp);
                        ++cp;
                    }

                    break;
                }

            case 'S':
                prScopedName(fp, va_arg(ap, scopedNameDef *), "::");
                break;

            case 'T':
                prTypeName(fp, va_arg(ap,argDef *));
                break;

            case 'u':
                fprintf(fp,"%u",va_arg(ap,unsigned));
                break;

            case 'U':
                {
                    classDef *cd = va_arg(ap, classDef *);

                    prScopedClassName(fp, cd->iff, cd, STRIP_NONE);
                    break;
                }

            case 'V':
                prScopedName(fp,
                        removeGlobalScope(va_arg(ap, scopedNameDef *)), "::");
                break;

            case 'x':
                fprintf(fp,"0x%08x",va_arg(ap,unsigned));
                break;

            case 'X':
                generateThrowSpecifier(va_arg(ap,throwArgs *),fp);
                break;

            case '\n':
                fputc('\n',fp);
                ++currentLineNr;
                break;

            case '\0':
                fputc('%',fp);
                --fmt;
                break;

            default:
                fputc(ch,fp);
            }
        }
        else if (ch == '\n')
        {
            fputc('\n',fp);
            ++currentLineNr;
        }
        else
            fputc(ch,fp);

    va_end(ap);
}


/*
 * Generate the symbolic name of a cached name.
 */
static void prCachedName(FILE *fp, nameDef *nd, const char *prefix)
{
    prcode(fp, "%s", prefix);

    /*
     * If the name seems to be a template then just use the offset to ensure
     * that it is unique.
     */
    if (strchr(nd->text, '<') != NULL)
        prcode(fp, "%d", nd->offset);
    else
    {
        const char *cp;

        /* Handle C++ and Python scopes. */
        for (cp = nd->text; *cp != '\0'; ++cp)
        {
            char ch = *cp;

            if (ch == ':' || ch == '.')
                ch = '_';

            prcode(fp, "%c", ch);
        }
    }
}


/*
 * Generate the C++ name of an overloaded function.
 */
static void prOverloadName(FILE *fp, overDef *od)
{
    const char *pt1, *pt2;

    pt1 = "operator";

    switch (od->common->slot)
    {
    case add_slot:
        pt2 = "+";
        break;

    case sub_slot:
        pt2 = "-";
        break;

    case mul_slot:
        pt2 = "*";
        break;

    case truediv_slot:
        pt2 = "/";
        break;

    case mod_slot:
        pt2 = "%";
        break;

    case and_slot:
        pt2 = "&";
        break;

    case or_slot:
        pt2 = "|";
        break;

    case xor_slot:
        pt2 = "^";
        break;

    case lshift_slot:
        pt2 = "<<";
        break;

    case rshift_slot:
        pt2 = ">>";
        break;

    case iadd_slot:
        pt2 = "+=";
        break;

    case isub_slot:
        pt2 = "-=";
        break;

    case imul_slot:
        pt2 = "*=";
        break;

    case itruediv_slot:
        pt2 = "/=";
        break;

    case imod_slot:
        pt2 = "%=";
        break;

    case iand_slot:
        pt2 = "&=";
        break;

    case ior_slot:
        pt2 = "|=";
        break;

    case ixor_slot:
        pt2 = "^=";
        break;

    case ilshift_slot:
        pt2 = "<<=";
        break;

    case irshift_slot:
        pt2 = ">>=";
        break;

    case invert_slot:
        pt2 = "~";
        break;

    case call_slot:
        pt2 = "()";
        break;

    case getitem_slot:
        pt2 = "[]";
        break;

    case lt_slot:
        pt2 = "<";
        break;

    case le_slot:
        pt2 = "<=";
        break;

    case eq_slot:
        pt2 = "==";
        break;

    case ne_slot:
        pt2 = "!=";
        break;

    case gt_slot:
        pt2 = ">";
        break;

    case ge_slot:
        pt2 = ">=";
        break;

    default:
        pt1 = "";
        pt2 = od->cppname;
    }

    if (fp != NULL)
        fprintf(fp, "%s%s", pt1, pt2);
    else
        errorAppend("%s%s", pt1, pt2);
}


/*
 * Generate a scoped name with the given separator string.
 */
static void prScopedName(FILE *fp, scopedNameDef *snd, char *sep)
{
    while (snd != NULL)
    {
        fprintf(fp, "%s", snd->name);

        if ((snd = snd->next) != NULL)
            fprintf(fp, "%s", sep);
    }
}


/*
 * Generate a scoped class name.  Protected classes have to be explicitly
 * scoped.
 */
static void prScopedClassName(FILE *fp, ifaceFileDef *scope, classDef *cd,
        int strip)
{
    if (generating_c)
        fprintf(fp, "%s ", (isUnion(cd) ? "union" : "struct"));

    if (useTemplateName(cd))
    {
        prTemplateType(fp, scope, cd->td, strip);
    }
    else if (isProtectedClass(cd))
    {
        /* This should never happen. */
        if (scope == NULL)
            scope = cd->iff;

        prcode(fp, "sip%C::sip%s", scope->fqcname, classBaseName(cd));
    }
    else
    {
        prScopedName(fp, stripScope(classFQCName(cd), strip), "::");
    }
}


/*
 * Generate a type name to be used as part of an identifier name.
 */
static void prTypeName(FILE *fp, argDef *ad)
{
    scopedNameDef *snd;

    switch (ad->atype)
    {
    case struct_type:
    case union_type:
        snd = ad->u.sname;
        break;

    case defined_type:
        snd = ad->u.snd;
        break;

    case enum_type:
        snd = ad->u.ed->fqcname;
        break;

    case mapped_type:
        snd = ad->u.mtd->iff->fqcname;
        break;

    case class_type:
        snd = classFQCName(ad->u.cd);
        break;

    default:
        /* This should never happen. */
        snd = NULL;
    }

    if (snd != NULL)
        prcode(fp, "%C", snd);
}


/*
 * Return TRUE if handwritten code uses the error flag.
 */
static int needErrorFlag(codeBlockList *cbl)
{
    return usedInCode(cbl, "sipError");
}


/*
 * Return TRUE if handwritten code uses the deprecated error flag.
 */
static int needOldErrorFlag(codeBlockList *cbl)
{
    return usedInCode(cbl, "sipIsErr");
}


/*
 * Return TRUE if the argument type means an instance needs to be created on
 * the heap to pass back to Python.
 */
static int needNewInstance(argDef *ad)
{
    return ((ad->atype == mapped_type || ad->atype == class_type) &&
        ((isReference(ad) && ad->nrderefs == 0) || (!isReference(ad) && ad->nrderefs == 1)) &&
        !isInArg(ad) && isOutArg(ad));
}


/*
 * Convert any protected arguments (ie. those whose type is unavailable outside
 * of a shadow class) to a fundamental type to be used instead (with suitable
 * casts).
 */
static void fakeProtectedArgs(signatureDef *sd)
{
    int a;
    argDef *ad = sd->args;

    for (a = 0; a < sd->nrArgs; ++a)
    {
        if (ad->atype == class_type && isProtectedClass(ad->u.cd))
        {
            ad->atype = fake_void_type;
            ad->nrderefs = 1;
            resetIsReference(ad);
        }
        else if (ad->atype == enum_type && isProtectedEnum(ad->u.ed))
            ad->atype = int_type;

        ++ad;
    }
}


/*
 * Reset and save any argument flags so that the argument will be rendered
 * exactly as defined in C++.
 */
static void normaliseArg(argDef *ad)
{
    if (ad->atype == class_type && isProtectedClass(ad->u.cd))
    {
        resetIsProtectedClass(ad->u.cd);
        setWasProtectedClass(ad->u.cd);
    }
    else if (ad->atype == enum_type && isProtectedEnum(ad->u.ed))
    {
        resetIsProtectedEnum(ad->u.ed);
        setWasProtectedEnum(ad->u.ed);
    }
}


/*
 * Reset and save any argument flags so that the signature will be rendered
 * exactly as defined in C++.
 */
void normaliseArgs(signatureDef *sd)
{
    int a;

    for (a = 0; a < sd->nrArgs; ++a)
        normaliseArg(&sd->args[a]);
}


/*
 * Restore any argument flags modified by normaliseArg().
 */
static void restoreArg(argDef *ad)
{
    if (ad->atype == class_type && wasProtectedClass(ad->u.cd))
    {
        resetWasProtectedClass(ad->u.cd);
        setIsProtectedClass(ad->u.cd);
    }
    else if (ad->atype == enum_type && wasProtectedEnum(ad->u.ed))
    {
        resetWasProtectedEnum(ad->u.ed);
        setIsProtectedEnum(ad->u.ed);
    }
}


/*
 * Restore any argument flags modified by normaliseArgs().
 */
void restoreArgs(signatureDef *sd)
{
    int a;

    for (a = 0; a < sd->nrArgs; ++a)
        restoreArg(&sd->args[a]);
}


/*
 * Return TRUE if a dealloc function is needed for a class.
 */
static int needDealloc(classDef *cd)
{
    if (cd->iff->type == namespace_iface)
        return FALSE;

    /* All of these conditions cause some code to be generated. */

    if (tracing)
        return TRUE;

    if (generating_c)
        return TRUE;

    if (cd->dealloccode != NULL)
        return TRUE;

    if (isPublicDtor(cd))
        return TRUE;

    if (hasShadow(cd))
        return TRUE;

    return FALSE;
}


/*
 * Return the argument name to use in a function definition for handwritten
 * code.
 */
static const char *argName(const char *name, codeBlockList *cbl)
{
    static const char noname[] = "";

    /* Always use the name in C code. */
    if (generating_c)
        return name;

    /* Use the name if it is used in the handwritten code. */
    if (usedInCode(cbl, name))
        return name;

    /* Don't use the name and avoid a compiler warning. */
    return noname;
}


/*
 * Returns TRUE if a string is used in code.
 */
static int usedInCode(codeBlockList *cbl, const char *str)
{
    while (cbl != NULL)
    {
        if (strstr(cbl->block->frag, str) != NULL)
            return TRUE;

        cbl = cbl->next;
    }

    return FALSE;
}


/*
 * Generate an assignment statement from a void * variable to a class instance
 * variable.
 */
static void generateClassFromVoid(classDef *cd, const char *cname,
        const char *vname, FILE *fp)
{
    if (generating_c)
        prcode(fp, "%U *%s = (%U *)%s", cd, cname, cd, vname);
    else
        prcode(fp, "%U *%s = reinterpret_cast<%U *>(%s)", cd, cname, cd,
                vname);
}


/*
 * Generate an assignment statement from a void * variable to a mapped type
 * variable.
 */
static void generateMappedTypeFromVoid(mappedTypeDef *mtd, const char *cname,
        const char *vname, FILE *fp)
{
    if (generating_c)
        prcode(fp, "%b *%s = (%b *)%s", &mtd->type, cname, &mtd->type, vname);
    else
        prcode(fp, "%b *%s = reinterpret_cast<%b *>(%s)", &mtd->type, cname,
                &mtd->type, vname);
}


/*
 * Returns TRUE if the argument has a type that requires an extra reference to
 * the originating object to be kept.
 */
static int keepPyReference(argDef *ad)
{
    if (ad->atype == ascii_string_type || ad->atype == latin1_string_type ||
            ad->atype == utf8_string_type || ad->atype == ustring_type ||
            ad->atype == sstring_type || ad->atype == string_type)
    {
        if (!isReference(ad) && ad->nrderefs > 0)
            return TRUE;
    }

    return FALSE;
}


/*
 * Return the encoding character for the given type.
 */
static char getEncoding(argDef *ad)
{
    char encoding;

    switch (ad->atype)
    {
    case ascii_string_type:
        encoding = 'A';
        break;

    case latin1_string_type:
        encoding = 'L';
        break;

    case utf8_string_type:
        encoding = '8';
        break;

    case wstring_type:
        encoding = ((ad->nrderefs == 0) ? 'w' : 'W');
        break;

    default:
        encoding = 'N';
    }

    return encoding;
}


/*
 * Return TRUE if a function/method has a docstring.
 */
static int hasMemberDocstring(sipSpec *pt, overDef *overs, memberDef *md)
{
    int auto_docstring = FALSE;
    overDef *od;

    /*
     * Check for any explicit docstrings and remember if there were any that
     * could be automatically generated.
     */
    for (od = overs; od != NULL; od = od->next)
    {
        if (od->common != md)
            continue;

        if (isPrivate(od) || isSignal(od))
            continue;

        if (od->docstring != NULL)
            return TRUE;

        if (docstrings)
            auto_docstring = TRUE;
    }

    if (noArgParser(md))
        return FALSE;

    return auto_docstring;
}


/*
 * Generate the docstring for all overloads of a function/method.  Return TRUE
 * if the docstring was entirely automatically generated.
 */
static int generateMemberDocstring(sipSpec *pt, overDef *overs, memberDef *md,
        int is_method, FILE *fp)
{
    int auto_docstring = TRUE;
    int is_first, all_auto, any_implied;
    static const char *newline = "\\n\"\n\"";
    overDef *od;

    /* See if all the docstrings are automatically generated. */
    all_auto = TRUE;
    any_implied = FALSE;

    for (od = overs; od != NULL; od = od->next)
    {
        if (od->common != md)
            continue;

        if (isPrivate(od) || isSignal(od))
            continue;

        if (od->docstring != NULL)
        {
            all_auto = FALSE;

            if (od->docstring->signature != discarded)
                any_implied = TRUE;
        }
    }

    /* Generate the docstring. */
    is_first = TRUE;

    for (od = overs; od != NULL; od = od->next)
    {
        if (od->common != md)
            continue;

        if (isPrivate(od) || isSignal(od))
            continue;

        if (!is_first)
        {
            prcode(fp, newline);

            /*
             * Insert a blank line if any explicit docstring wants to include a
             * signature.  This maintains compatibility with previous versions.
             */
            if (any_implied)
                prcode(fp, newline);
        }

        if (od->docstring != NULL)
        {
            if (od->docstring->signature == prepended)
            {
                generateMemberAutoDocstring(pt, od, is_method, fp);
                prcode(fp, newline);
            }

            generateDocstringText(od->docstring, fp);

            if (od->docstring->signature == appended)
            {
                prcode(fp, newline);
                generateMemberAutoDocstring(pt, od, is_method, fp);
            }

            auto_docstring = FALSE;
        }
        else if (all_auto || any_implied)
        {
            generateMemberAutoDocstring(pt, od, is_method, fp);
        }

        is_first = FALSE;
    }

    return auto_docstring;
}


/*
 * Generate the automatic docstring for a function/method.
 */
static void generateMemberAutoDocstring(sipSpec *pt, overDef *od,
        int is_method, FILE *fp)
{
    if (docstrings)
        dsOverload(pt, od, is_method, fp);
}


/*
 * Return TRUE if a class has a docstring.
 */
static int hasClassDocstring(sipSpec *pt, classDef *cd)
{
    int auto_docstring = FALSE;
    ctorDef *ct;

    /*
     * Check for any explicit docstrings and remember if there were any that
     * could be automatically generated.
     */
    if (cd->docstring != NULL)
        return TRUE;

    for (ct = cd->ctors; ct != NULL; ct = ct->next)
    {
        if (isPrivateCtor(ct))
            continue;

        if (ct->docstring != NULL)
            return TRUE;

        if (docstrings)
            auto_docstring = TRUE;
    }

    if (!canCreate(cd))
        return FALSE;

    return auto_docstring;
}


/*
 * Generate the docstring for a class.
 */
static void generateClassDocstring(sipSpec *pt, classDef *cd, FILE *fp)
{
    int is_first, all_auto, any_implied;
    static const char *newline = "\\n\"\n\"";
    ctorDef *ct;

    /* See if all the docstrings are automatically generated. */
    all_auto = (cd->docstring == NULL);
    any_implied = FALSE;

    for (ct = cd->ctors; ct != NULL; ct = ct->next)
    {
        if (isPrivateCtor(ct))
            continue;

        if (ct->docstring != NULL)
        {
            all_auto = FALSE;

            if (ct->docstring->signature != discarded)
                any_implied = TRUE;
        }
    }

    /* Generate the docstring. */
    if (all_auto)
        prcode(fp, "\\1");

    if (cd->docstring != NULL && cd->docstring->signature != prepended)
    {
        generateDocstringText(cd->docstring, fp);
        is_first = FALSE;
    }
    else
    {
        is_first = TRUE;
    }

    if (cd->docstring == NULL || cd->docstring->signature != discarded)
    {
        for (ct = cd->ctors; ct != NULL; ct = ct->next)
        {
            if (isPrivateCtor(ct))
                continue;

            if (!is_first)
            {
                prcode(fp, newline);

                /*
                 * Insert a blank line if any explicit docstring wants to
                 * include a signature.  This maintains compatibility with
                 * previous versions.
                 */
                if (any_implied)
                    prcode(fp, newline);
            }

            if (ct->docstring != NULL)
            {
                if (ct->docstring->signature == prepended)
                {
                    generateCtorAutoDocstring(pt, cd, ct, fp);
                    prcode(fp, newline);
                }

                generateDocstringText(ct->docstring, fp);

                if (ct->docstring->signature == appended)
                {
                    prcode(fp, newline);
                    generateCtorAutoDocstring(pt, cd, ct, fp);
                }
            }
            else if (all_auto || any_implied)
            {
                generateCtorAutoDocstring(pt, cd, ct, fp);
            }

            is_first = FALSE;
        }
    }

    if (cd->docstring != NULL && cd->docstring->signature == prepended)
    {
        if (!is_first)
        {
            prcode(fp, newline);
            prcode(fp, newline);
        }

        generateDocstringText(cd->docstring, fp);
    }
}


/*
 * Generate the automatic docstring for a ctor.
 */
static void generateCtorAutoDocstring(sipSpec *pt, classDef *cd, ctorDef *ct,
        FILE *fp)
{
    if (docstrings)
        pyiCtor(pt, pt->module, cd, ct, fp);
}


/*
 * Generate the text of a docstring.
 */
static void generateDocstringText(docstringDef *docstring, FILE *fp)
{
    const char *cp;

    for (cp = docstring->text; *cp != '\0'; ++cp)
    {
        if (*cp == '\n')
        {
            /* Ignore if this is the last character. */
            if (cp[1] != '\0')
                prcode(fp, "\\n\"\n\"");
        }
        else
        {
            if (*cp == '\\' || *cp == '\"')
                prcode(fp, "\\");

            prcode(fp, "%c", *cp);
        }
    }
}


/*
 * Generate the definition of a module's optional docstring.
 */
static void generateModDocstring(moduleDef *mod, FILE *fp)
{
    if (mod->docstring != NULL)
    {
        prcode(fp,
"\n"
"PyDoc_STRVAR(doc_mod_%s, \"", mod->name);

        generateDocstringText(mod->docstring, fp);

        prcode(fp, "\");\n"
            );
    }
}


/*
 * Generate a void* cast for an argument if needed.
 */
static void generateVoidPtrCast(argDef *ad, FILE *fp)
{
    /*
     * Generate a cast if the argument's type was a typedef.  This allows us to
     * use typedef's to void* to hide something more complex that we don't
     * handle.
     */
    if (ad->original_type != NULL)
        prcode(fp, "(%svoid *)", (isConstArg(ad) ? "const " : ""));
}


/*
 * Declare the use of the limited API.
 */
static void declareLimitedAPI(int py_debug, moduleDef *mod, FILE *fp)
{
    if (!py_debug && (mod == NULL || useLimitedAPI(mod)))
        prcode(fp,
"\n"
"#if !defined(Py_LIMITED_API)\n"
"#define Py_LIMITED_API\n"
"#endif\n"
            );
}


/*
 * Generate the PyQt signals table.
 */
static int generatePluginSignalsTable(sipSpec *pt, classDef *cd, FILE *fp)
{
    int is_signals = FALSE;

    if (isQObjectSubClass(cd))
    {
        memberDef *md;

        /* The signals must be grouped by name. */
        for (md = cd->members; md != NULL; md = md->next)
        {
            overDef *od;
            int membernr = md->membernr;

            for (od = cd->overs; od != NULL; od = od->next)
            {
                if (od->common != md || !isSignal(od))
                    continue;

                if (membernr >= 0)
                {
                    /* See if there is a non-signal overload. */

                    overDef *nsig;

                    for (nsig = cd->overs; nsig != NULL; nsig = nsig->next)
                        if (nsig != od && nsig->common == md && !isSignal(nsig))
                            break;

                    if (nsig == NULL)
                        membernr = -1;
                }

                if (!is_signals)
                {
                    is_signals = TRUE;

                    if (generatePyQtEmitters(cd, fp) < 0)
                        return -1;

                    prcode(fp,
"\n"
"\n"
"/* Define this type's signals. */\n"
"static const pyqt%cQtSignal signals_%C[] = {\n"
                        , (pluginPyQt5(pt) ? '5' : '6'), classFQCName(cd));
                }

                /*
                 * We enable a hack that supplies any missing optional
                 * arguments.  We only include the version with all arguments
                 * and provide an emitter function which handles the optional
                 * arguments.
                 */
                generateSignalTableEntry(pt, cd, od, membernr,
                        hasOptionalArgs(od), fp);

                membernr = -1;
            }
        }

        if (is_signals)
            prcode(fp,
"    {SIP_NULLPTR, SIP_NULLPTR, SIP_NULLPTR, SIP_NULLPTR}\n"
"};\n"
                );
    }

    return is_signals;
}


/*
 * Generate any extended mapped type definition data for PyQt6.  Return TRUE if
 * there was something generated.
 */
static int generatePyQt6MappedTypePlugin(sipSpec *pt, mappedTypeDef *mtd,
        FILE *fp)
{
    if (mtd->pyqt_flags == 0)
        return FALSE;

    prcode(fp,
"\n"
"\n"
"static pyqt6MappedTypePluginDef plugin_%L = {%u};\n"
        , mtd->iff , mtd->pyqt_flags);

    return TRUE;
}


/*
 * Generate any extended class definition data for PyQt.  Return TRUE if there
 * was something generated.
 */
static int generatePyQtClassPlugin(sipSpec *pt, classDef *cd, FILE *fp)
{
    int is_signals = generatePluginSignalsTable(pt, cd, fp);

    if (is_signals < 0)
        return -1;

    /* The PyQt6 support code doesn't assume the structure is generated. */
    if (pluginPyQt6(pt))
    {
        int generated = is_signals;

        if (isQObjectSubClass(cd) && !noPyQtQMetaObject(cd))
            generated = TRUE;

        if (cd->pyqt_interface != NULL)
            generated = TRUE;

        if (!generated)
            return FALSE;
    }

    prcode(fp,
"\n"
"\n"
"static pyqt%cClassPluginDef plugin_%L = {\n"
        , (pluginPyQt5(pt) ? '5' : '6'), cd->iff);

    if (isQObjectSubClass(cd) && !noPyQtQMetaObject(cd))
        prcode(fp,
"    &%U::staticMetaObject,\n"
            , cd);
    else
        prcode(fp,
"    SIP_NULLPTR,\n"
            );

    if (pluginPyQt5(pt))
        prcode(fp,
"    %u,\n"
            , cd->pyqt_flags);

    if (is_signals)
        prcode(fp,
"    signals_%C,\n"
            , classFQCName(cd));
    else
        prcode(fp,
"    SIP_NULLPTR,\n"
            );

    if (cd->pyqt_interface != NULL)
        prcode(fp,
"    \"%s\"\n"
            , cd->pyqt_interface);
    else
        prcode(fp,
"    SIP_NULLPTR\n"
            );

    prcode(fp,
"};\n"
        );

    return TRUE;
}


/*
 * Generate the entries in a table of PyMethodDef for global functions.
 */
static void generateGlobalFunctionTableEntries(sipSpec *pt, moduleDef *mod,
        memberDef *members, FILE *fp)
{
    memberDef *md;

    for (md = members; md != NULL; md = md->next)
    {
        if (md->slot == no_slot)
        {
            prcode(fp,
"        {%N, ", md->pyname);

            if (noArgParser(md) || useKeywordArgs(md))
                prcode(fp, "SIP_MLMETH_CAST(func_%s), METH_VARARGS|METH_KEYWORDS", md->pyname->text);
            else
                prcode(fp, "func_%s, METH_VARARGS", md->pyname->text);

            if (hasMemberDocstring(pt, mod->overs, md))
                prcode(fp, ", doc_%s},\n"
                    , md->pyname->text);
            else
                prcode(fp, ", SIP_NULLPTR},\n"
                    );
        }
    }
}


/*
 * Generate a template type.
 */
static void prTemplateType(FILE *fp, ifaceFileDef *scope, templateDef *td,
        int strip)
{   
    static const char tail[] = ">";
    int a;
    
    if (prcode_xml)
        strip = STRIP_GLOBAL;

    prcode(fp, "%S%s", stripScope(td->fqname, strip),
            (prcode_xml ? "&lt;" : "<"));
    
    for (a = 0; a < td->types.nrArgs; ++a)
    {   
        if (a > 0)
            prcode(fp, ", ");
        
        generateBaseType(scope, &td->types.args[a], TRUE, strip, fp);
    }       
    
    prcode(fp, (prcode_xml ? "&gt;" : tail));
}


/*
 * Strip the leading scopes from a scoped name as required.
 */
static scopedNameDef *stripScope(scopedNameDef *snd, int strip)
{
    if (strip != STRIP_NONE)
    {
        snd = removeGlobalScope(snd);

        while (strip-- > 0 && snd->next != NULL)
            snd = snd->next;
    }

    return snd;
}


/*
 * Generate the scope of a member of an unscoped enum.
 */
static void prEnumMemberScope(enumMemberDef *emd, FILE *fp)
{
    classDef *ecd = emd->ed->ecd;

    if (isProtectedEnum(emd->ed))
        prcode(fp, "sip%C", classFQCName(ecd));
    else if (isProtectedClass(ecd))
        prcode(fp, "%U", ecd);
    else
        prcode(fp, "%S", classFQCName(ecd));
}


/*
 * Generate the inclusion of sip.h.
 */
static void generate_include_sip_h(moduleDef *mod, FILE *fp)
{
    if (isPY_SSIZE_T_CLEAN(mod))
        prcode(fp,
"\n"
"#define PY_SSIZE_T_CLEAN\n"
            );

    prcode(fp,
"\n"
"#include \"sip.h\"\n"
        );
}


/*
 * Return the number of members an enum has.
 */
static int get_nr_members(const enumDef *ed)
{
    int nr = 0;
    const enumMemberDef *emd;

    for (emd = ed->members; emd != NULL; emd = emd->next)
        ++nr;

    return nr;
}


/*
 * Return the interface file of the Python scope corresponding to a C/C++
 * scope.
 */
ifaceFileDef *pyScopeIface(classDef *cd)
{
    classDef *scope = pyScope(cd);

    return (scope != NULL ? scope->iff : NULL);
}


/*
 * Return the interface file of the Python scope corresponding to the C/C++
 * scope of an enum.
 */
ifaceFileDef *pyEnumScopeIface(enumDef *ed)
{
    if (ed->ecd != NULL)
        return pyScopeIface(ed->ecd);

    if (ed->emtd != NULL)
        return ed->emtd->iff;

    return NULL;
}


/*
 * Generate an enum member.
 */
static void generateEnumMember(FILE *fp, enumMemberDef *emd, mappedTypeDef *mtd)
{
    if (!generating_c)
    {
        prcode(fp, "static_cast<int>(");

        if (!isNoScope(emd->ed))
        {
            if (isScopedEnum(emd->ed))
                prcode(fp, "::%s", emd->ed->cname->text);
            else if (emd->ed->ecd != NULL)
                prEnumMemberScope(emd, fp);
            else if (mtd != NULL)
                prcode(fp, "%S", mtd->iff->fqcname);

            prcode(fp, "::");
        }
    }

    prcode(fp, "%s", emd->cname);

    if (!generating_c)
        prcode(fp, ")");

}


/*
 * Return TRUE if a type needs user state to be provided.
 */
static int typeNeedsUserState(argDef *ad)
{
    return (ad->atype == mapped_type && needsUserState(ad->u.mtd));
}


/*
 * Return the suffix for functions that have a variant that supports a user
 * state.
 */
static const char *userStateSuffix(argDef *ad)
{
    return (abiVersion >= ABI_13_0 && typeNeedsUserState(ad)) ? "US" : "";
}


/*
 * Generate the exception handler for a module.
 */
static void generateExceptionHandler(sipSpec *pt, moduleDef *mod, FILE *fp)
{
    exceptionDef *xd;
    int need_decl = TRUE;

    for (xd = pt->exceptions; xd != NULL; xd = xd->next)
        if (xd->iff->module == mod)
        {
            if (need_decl)
            {
                prcode(fp,
"\n"
"\n"
"/* Handle the exceptions defined in this module. */\n"
"bool sipExceptionHandler_%s(std::exception_ptr sipExcPtr)\n"
"{\n"
"    try {\n"
"        std::rethrow_exception(sipExcPtr);\n"
"    }\n"
                    , mod->name);

                need_decl = FALSE;
            }

            generateCatchBlock(mod, xd, NULL, fp, FALSE);
        }

    if (!need_decl)
        prcode(fp,
"    catch (...) {}\n"
"\n"
"    return false;\n"
"}\n"
            );
}
 
 
/*
 * Append a string to a list of them.
 */
void appendString(stringList **headp, const char *s)
{
    stringList *sl;

    /* Create the new entry. */
    sl = sipMalloc(sizeof (stringList));
    sl->s = s;
    sl->next = NULL;

    /* Append it to the list. */
    while (*headp != NULL)
        headp = &(*headp)->next;

    *headp = sl;
}


/*
 * Return TRUE if the given qualifier is excluded.
 */
static int excludedFeature(stringList *xsl, qualDef *qd)
{
    while (xsl != NULL)
    {
        if (strcmp(qd->name, xsl->s) == 0)
            return TRUE;

        xsl = xsl->next;
    }

    return !qd->default_enabled;
}


/*
 * Return TRUE if the PyQt5 plugin was specified.
 */
int pluginPyQt5(sipSpec *pt)
{
    return stringFind(pt->plugins, "PyQt5");
}


/*
 * Return TRUE if the PyQt6 plugin was specified.
 */
int pluginPyQt6(sipSpec *pt)
{
    return stringFind(pt->plugins, "PyQt6");
}


/*
 * Return TRUE if a list of strings contains a given entry.
 */
static int stringFind(stringList *sl, const char *s)
{
    while (sl != NULL)
    {
        if (strcmp(sl->s, s) == 0)
            return TRUE;

        sl = sl->next;
    }

    return FALSE;
}


/*
 * Remove any explicit global scope.
 */
scopedNameDef *removeGlobalScope(scopedNameDef *snd)
{
    return ((snd != NULL && snd->name[0] == '\0') ? snd->next : snd);
}


/*
 * Return a pointer to the tail part of a scoped name.
 */
char *scopedNameTail(scopedNameDef *snd)
{
    if (snd == NULL)
        return NULL;

    while (snd->next != NULL)
        snd = snd->next;

    return snd->name;
}


/*
 * Return TRUE if the given qualifier is needed.
 */
static int selectedQualifier(stringList *needed_qualifiers, qualDef *qd)
{
    stringList *sl;

    for (sl = needed_qualifiers; sl != NULL; sl = sl->next)
        if (strcmp(qd->name, sl->s) == 0)
            return qd->default_enabled;

    return FALSE;
}


/*
 * Put a scoped name to stderr.
 */
static void errorScopedName(scopedNameDef *snd)
{
    while (snd != NULL)
    {
        errorAppend("%s", snd->name);

        snd = snd -> next;

        if (snd != NULL)
            errorAppend("::");
    }
}


/*
 * Compare two signatures and return TRUE if they are the same.
 */
static int sameSignature(signatureDef *sd1, signatureDef *sd2, int strict)
{
    int a;

    if (strict)
    {
        /* The number of arguments must be the same. */
        if (sd1 -> nrArgs != sd2 -> nrArgs)
            return FALSE;
    }
    else
    {
        int na1, na2;

        /* We only count the compulsory arguments. */
        na1 = 0;

        for (a = 0; a < sd1 -> nrArgs; ++a)
        {
            if (sd1 -> args[a].defval != NULL)
                break;

            ++na1;
        }

        na2 = 0;

        for (a = 0; a < sd2 -> nrArgs; ++a)
        {
            if (sd2 -> args[a].defval != NULL)
                break;

            ++na2;
        }

        if (na1 != na2)
            return FALSE;
    }

    /* The arguments must be the same. */
    for (a = 0; a < sd1 -> nrArgs; ++a)
    {
        if (!strict && sd1 -> args[a].defval != NULL)
            break;

        if (!sameArgType(&sd1 -> args[a],&sd2 -> args[a],strict))
            return FALSE;
    }

    /* Must be the same if we've got this far. */
    return TRUE;
}


#define pyAsString(t)   ((t) == ustring_type || (t) == sstring_type || \
            (t) == string_type || (t) == ascii_string_type || \
            (t) == latin1_string_type || (t) == utf8_string_type)
#define pyAsFloat(t)    ((t) == cfloat_type || (t) == float_type || \
            (t) == cdouble_type || (t) == double_type)
#define pyAsInt(t)  ((t) == bool_type || (t) == hash_type || \
            (t) == ssize_type || (t) == size_type || (t) == byte_type || \
            (t) == sbyte_type || (t) == ubyte_type || (t) == short_type || \
            (t) == ushort_type || (t) == cint_type || (t) == int_type || \
            (t) == uint_type)
#define pyAsLong(t) ((t) == long_type || (t) == longlong_type)
#define pyAsULong(t)    ((t) == ulong_type || (t) == ulonglong_type)
#define pyAsAuto(t) ((t) == bool_type || \
            (t) == byte_type || (t) == sbyte_type || (t) == ubyte_type || \
            (t) == short_type || (t) == ushort_type || \
            (t) == int_type || (t) == uint_type || \
            (t) == float_type || (t) == double_type)
#define pyIsConstrained(t)  ((t) == cbool_type || (t) == cint_type || \
            (t) == cfloat_type || (t) == cdouble_type)

/*
 * Compare two argument types and return TRUE if they are the same.  "strict"
 * means as C++ would see it, rather than Python.
 */
static int sameArgType(argDef *a1, argDef *a2, int strict)
{
    /* The references must be the same. */
    if (isReference(a1) != isReference(a2) || a1->nrderefs != a2->nrderefs)
        return FALSE;

    if (strict)
    {
        /* The const should be the same. */
        if (isConstArg(a1) != isConstArg(a2))
            return FALSE;

        return sameBaseType(a1,a2);
    }

    /* If both are constrained fundamental types then the types must match. */
    if (pyIsConstrained(a1->atype) && pyIsConstrained(a2->atype))
        return (a1->atype == a2->atype);

    if (abiVersion >= ABI_13_0)
    {
        /* Anonymous enums are ints. */
        if ((pyAsInt(a1->atype) && a2->atype == enum_type && a2->u.ed->fqcname == NULL) ||
            (a1->atype == enum_type && a1->u.ed->fqcname == NULL && pyAsInt(a2->atype)))
            return TRUE;
    }
    else
    {
        /* An unconstrained enum also acts as a (very) constrained int. */
        if ((pyAsInt(a1->atype) && a2->atype == enum_type && !isConstrained(a2)) ||
            (a1->atype == enum_type && !isConstrained(a1) && pyAsInt(a2->atype)))
            return TRUE;
    }

    /* Python will see all these as strings. */
    if (pyAsString(a1->atype) && pyAsString(a2->atype))
        return TRUE;

    /* Python will see all these as floats. */
    if (pyAsFloat(a1->atype) && pyAsFloat(a2->atype))
        return TRUE;

    /* Python will see all these as ints. */
    if (pyAsInt(a1->atype) && pyAsInt(a2->atype))
        return TRUE;

    /* Python will see all these as longs. */
    if (pyAsLong(a1->atype) && pyAsLong(a2->atype))
        return TRUE;

    /* Python will see all these as unsigned longs. */
    if (pyAsULong(a1->atype) && pyAsULong(a2->atype))
        return TRUE;

    /* Python will automatically convert between these. */
    if (pyAsAuto(a1->atype) && pyAsAuto(a2->atype))
        return TRUE;

    /* All the special cases have been handled. */
    return sameBaseType(a1, a2);
}


/*
 * Compare two basic types and return TRUE if they are the same.
 */
static int sameBaseType(argDef *a1, argDef *a2)
{
    /* The types must be the same. */
    if (a1->atype != a2->atype)
    {
        /*
         * If we are comparing a template with those that have already been
         * used to instantiate a class or mapped type then we need to compare
         * with the class or mapped type name.
         */
        if (a1->atype == class_type && a2->atype == defined_type)
            return compareScopedNames(a1->u.cd->iff->fqcname, a2->u.snd) == 0;

        if (a1->atype == defined_type && a2->atype == class_type)
            return compareScopedNames(a2->u.cd->iff->fqcname, a1->u.snd) == 0;

        if (a1->atype == mapped_type && a2->atype == defined_type)
            return compareScopedNames(a1->u.mtd->iff->fqcname, a2->u.snd) == 0;

        if (a1->atype == defined_type && a2->atype == mapped_type)
            return compareScopedNames(a2->u.mtd->iff->fqcname, a1->u.snd) == 0;

        if (a1->atype == enum_type && a2->atype == defined_type)
            return compareScopedNames(a1->u.ed->fqcname, a2->u.snd) == 0;

        if (a1->atype == defined_type && a2->atype == enum_type)
            return compareScopedNames(a2->u.ed->fqcname, a1->u.snd) == 0;

        return FALSE;
    }

    switch (a1->atype)
    {
    case class_type:
        if (a1->u.cd != a2->u.cd)
            return FALSE;

        break;

    case enum_type:
        if (a1->u.ed != a2->u.ed)
            return FALSE;

        break;

    case template_type:
        {
            int a;
            templateDef *td1, *td2;

            td1 = a1->u.td;
            td2 = a2->u.td;

            if (compareScopedNames(td1->fqname, td2->fqname) != 0 ||
                    td1->types.nrArgs != td2->types.nrArgs)
                return FALSE;

            for (a = 0; a < td1->types.nrArgs; ++a)
            {
                argDef *td1ad = &td1->types.args[a];
                argDef *td2ad = &td2->types.args[a];

                if (td1ad->nrderefs != td2ad->nrderefs)
                    return FALSE;

                if (!sameBaseType(td1ad, td2ad))
                    return FALSE;
            }

            break;
        }

    case struct_type:
    case union_type:
        if (compareScopedNames(a1->u.sname, a2->u.sname) != 0)
            return FALSE;

        break;

    case defined_type:
        if (compareScopedNames(a1->u.snd, a2->u.snd) != 0)
            return FALSE;

        break;

    case mapped_type:
        if (a1->u.mtd != a2->u.mtd)
            return FALSE;

        break;

    /* Suppress a compiler warning. */
    default:
        ;
    }

    /* Must be the same if we've got this far. */
    return TRUE;
}


/*
 * The equivalent of strcmp() for scoped names.
 */
int compareScopedNames(scopedNameDef *snd1, scopedNameDef *snd2)
{
    /* Strip the global scope if the target doesn't specify it. */
    if (snd2->name[0] != '\0')
        snd1 = removeGlobalScope(snd1);

    while (snd1 != NULL && snd2 != NULL)
    {
        int res = strcmp(snd1->name, snd2->name);

        if (res != 0)
            return res;

        snd1 = snd1->next;
        snd2 = snd2->next;
    }

    if (snd1 == NULL)
        return (snd2 == NULL ? 0 : -1);

    return 1;
}


/*
 * Return the fully qualified C/C++ name for a generated type.
 */
static scopedNameDef *getFQCNameOfType(argDef *ad)
{
    scopedNameDef *snd;

    switch (ad->atype)
    {
    case class_type:
        snd = classFQCName(ad->u.cd);
        break;

    case mapped_type:
        snd = ad->u.mtd->iff->fqcname;
        break;

    case enum_type:
        snd = ad->u.ed->fqcname;
        break;

    default:
        /* Suppress a compiler warning. */
        snd = NULL;
    }

    return snd;
}


/*
 * Return the method of a class with a given name.
 */
memberDef *findMethod(classDef *cd, const char *name)
{
    memberDef *md;

    for (md = cd->members; md != NULL; md = md->next)
        if (strcmp(md->pyname->text, name) == 0)
            break;

    return md;
}


/*
 * Generate the docstring for a single API overload.
 */
static void dsOverload(sipSpec *pt, overDef *od, int is_method, FILE *fp)
{
    pyiOverload(pt, pt->module, od, is_method, fp);
}


/*
 * Return the name to use for an argument.
 */
static const char *get_argument_name(argDef *arg, int arg_nr,
        moduleDef *module)
{
    static char buf[50];

    if (useArgNames(module) && arg->name != NULL && arg->atype != ellipsis_type)
        return arg->name->text;

    sprintf(buf, "a%d", arg_nr);

    return &buf[0];
}


/*
 * Generate extra support for sequences because the class has an overload that
 * has been annotated with __len__.
 */
static void generateSequenceSupport(classDef *klass, overDef *overload,
        moduleDef *module, FILE *fp)
{
    argDef *arg0 = &overload->pysig.args[0];

    /* We require a single int argument. */
    if (!(overload->pysig.nrArgs == 1 && (pyAsInt(arg0->atype) || pyAsLong(arg0->atype) || pyAsULong(arg0->atype))))
        return;

    /*
     * At the moment all we do is check that an index to __getitem__ is within
     * range so that the class supports Python iteration.  In the future we
     * should add support for negative indices, slices, __setitem__ and
     * __delitem__ (which will require enhancements to the sip module ABI).
     */
    if (overload->common->slot == getitem_slot)
    {
        const char *index_arg = get_argument_name(arg0, 0, module);

        prcode(fp,
"            if (%s < 0 || %s >= sipCpp->%s())\n"
"            {\n"
"                PyErr_SetNone(PyExc_IndexError);\n"
"                return SIP_NULLPTR;\n"
"            }\n"
"\n"
            , index_arg, index_arg, klass->len_cpp_name);
    }
}
