#ifndef BONGO_CAT_MEMORY_POLICY_H
#define BONGO_CAT_MEMORY_POLICY_H

void bongo_cat_memory_policy_model_loaded(void);
void bongo_cat_memory_policy_frame_presented(void);
void bongo_cat_memory_policy_idle(void);
void bongo_cat_memory_policy_ui_loaded(void);
void bongo_cat_memory_policy_ui_frame_presented(void);
/* Teardown may leave no further UI/model frames. Service reclamation from
   the main loop, outside synchronous model loading and native callbacks. */
void bongo_cat_memory_policy_ui_released(void);
void bongo_cat_memory_policy_poll(void);

#endif
