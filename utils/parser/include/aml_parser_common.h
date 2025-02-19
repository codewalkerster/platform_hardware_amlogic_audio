/*
 * Copyright (C) 2024 Amlogic Corporation.
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


#ifndef _AML_PARSER_COMMON_H_
#define _AML_PARSER_COMMON_H_

typedef int (*Func_Write_CallBack)(void *priObject, void *aBuffer, void *pHandle);

typedef struct aml_parser_data_callback {
    union {
        void *pAmlStream;
        void *pAmlParser;
    } common;
    Func_Write_CallBack callback;
} aml_parser_data_callback_t;


typedef int (*F_Parser_Init)(void **ppParserHandle);
typedef int (*F_Parser_DeInit)(void *pParserHandle);
typedef int (*F_Parser_Process)(void *pParserHandle, const void *inABuffer, void *outABuffer, void *parser_callback);
typedef int (*F_Parser_Reset)(void *pParserHandle);
typedef int (*F_Parser_Flush)(void *pParserHandle);
typedef int (*F_Parser_get_format)(void *pParserHandle);

typedef struct aml_parser_func {
    F_Parser_Init                  f_init;
    F_Parser_DeInit                f_deinit;
    F_Parser_Process               f_process;
    F_Parser_Reset                 f_reset;
    F_Parser_Flush                 f_flush;
    F_Parser_get_format            f_get_format;
} aml_parser_func_t;

#endif  //_AML_PARSER_COMMON_H_
