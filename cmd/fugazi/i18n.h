#ifndef FUGAZI_I18N_H
#define FUGAZI_I18N_H
void fz_i18n_init(const char *res_dir);
const char *fz_t(const char *key);
#define T(s) fz_t(s)
#endif
