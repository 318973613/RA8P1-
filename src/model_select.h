/* Model selection switch for build and runtime. */
#ifndef __MODEL_SELECT_H__
#define __MODEL_SELECT_H__

/* 0: gesture model only
 * 1: face detection + embedding pipeline
 */
#define APP_USE_FACE_PIPELINE 1

#if APP_USE_FACE_PIPELINE
#define APP_USE_CLASSIFIER 1
#else
#define APP_USE_CLASSIFIER 0
#endif


#endif
