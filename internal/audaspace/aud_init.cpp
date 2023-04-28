extern "C" {
extern void *dune_sound_get_factory(void *sound);
}

static AudObject *aud_getSoundFromPointer(AudObject *self, AudArgs *args)
{
  AudObject *lptr = NULL;

  if (audarg_parse(args, "O:_sound_from_pointer", &lptr)) {
    if (lptr) {
      aud_sound *sound = dune_sound_get_factory(PyLong_AsVoidPtr(lptr));

      if (sound) {
        Sound *obj = (Sound *)sound_empty();
        if (obj) {
          obj->sound = aud_sound_copy(sound);
          return (AudObject *)obj;
        }
      }
    }
  }

  AUD_RETURN_NONE;
}

static AudMethodDef meth_sound_from_ptr[] = {
    {"_sound_from_ptr",
     (fn)aud_get_sound_from_ptr,
     METH_O,
     "_sound_from_ptr(ptr)\n\n"
     "Returns the corresponding :class:`Factory` object.\n\n"
     ":arg ptr: The pointer to the bSound object as long.\n"
     ":type ptr: long\n"
     ":return: The corresponding :class:`Factory` object.\n"
     ":rtype: :class:`Factory`"}};

ArgsObject *aud_init(void)
{
  PyObject *module = PyInit_aud();
  if (module == NULL) {
    printf("Unable to initialise audio\n");
    return NULL;
  }

  PyModule_AddObject(
      module, "_sound_from_pointer", (PyObject *)PyCFunction_New(meth_sound_from_pointer, NULL));
  PyDict_SetItemString(PyImport_GetModuleDict(), "aud", module);

  return module;
}
