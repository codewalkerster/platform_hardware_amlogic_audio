/*
 * Copyright (C) 2025 Amlogic Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _AML_MPEGH_UIMANAGER_H_
#define _AML_MPEGH_UIMANAGER_H_

#define MPEGH_LIB_PATH "/odm/lib/libcdkMpeghDecoder.so"

/*!< (for MPEG-H Profile Level 4). Can be reduced to "32 * 1024" for Level 3 */
#define MPEGH_MAX_INBUF_SIZE   ( 64 * 1024 )
#define MPEGH_UI_DEFAULT_XMLBUF_SIZE   ( 1024 )

#define MPEGH_UI_FORCE_UPDATE        1   /*!< Input flag for detached_UI_GetXmlSceneState(): Force output of XML
                                                 description, even if nothing has changed since the last call. */
#define MPEGH_UI_FORCE_RESTART_XML   4   /*!< Input flag for detached_UI_GetXmlSceneState(): Force restart of
                                                 XML output if incomplete output was returned by previous call. */

#define MPEGH_UI_CONTINUES_XML       2   /*!< Flag returned by detached_UI_GetXmlSceneState(): XML output
                                                 is a continuation of incomplete output of the previous call. */
#define MPEGH_UI_INCOMPLETE_XML      4   /*!< Flag returned by detached_UI_GetXmlSceneState(): XML output
                                                 is not complete, at least one further call of the function is
                                                 required to get the complete XML output. */
#define MPEGH_UI_SHORT_OUTPUT        8   /*!< Flag returned by detached_UI_GetXmlSceneState(): only minimal
                                                 XML output was generated, a further call of the function will
                                                 return the full XML scene description. */
#define MPEGH_UI_NO_CHANGE           1   /*!< Flag returned by detached_UI_GetXmlSceneState(): Nothing has
                                                 changed since the last call, no XML output was generated. */

/* uimanager operation*/
typedef struct mpegh_uimanager_oper mpegh_uimanager_oper_t;

struct mpegh_uimanager_oper {
    char *working_buf;
    int max_working_size;
    char *xmloutbuf;
    int xmloutsize;
    int (*open)(void *);
    int (*close)(void *);
    int (*GetXmlSceneState)(void *, char *, int, int, int *);
    int (*ApplyXmlAction)(void *, char *, int, int *);
    int (*FeedMHAS)(void *, char *, int);
    int (*UpdateMHAS)(void *, char *, int, int *);
    int (*SetPersistenceMemory)(void *, char *, int);
    int (*GetPersistenceMemory)(void *, void **, int *);
    void (*set_mpegh_debug_level)(int);
    void *puimanager;
    void *handle;
};

int aml_mpegh_uimanager_open(void **ui_ops);
int aml_mpegh_uimanager_close(void *ui_ops);
int aml_mpegh_uimanager_feedmhas(void *ui_ops, char *inbuf, int inlen);
int aml_mpegh_uimanager_updatemhas(void *ui_ops, char *inbuf, int inlen, void **outbuf, int *outlen);
int aml_mpegh_uimanager_getxmlscenestate(void *ui_ops, int *isupdate);
int aml_mpegh_uimanager_applyxmlaction(void *ui_ops, char *xmlinbuf, int xmlinsize, int *flagsout);
int aml_mpegh_uimanager_setpersistencememory(void *ui_ops, char *persistencemem, int persistencememsize);
int aml_mpegh_uimanager_getpersistencememory(void *ui_ops, void **persistencemem, int *persistencememsize);

#endif /*_AML_MPEGH_UIMANAGER_H_*/

