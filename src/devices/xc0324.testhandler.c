static int testhandler_callback(bitbuffer_t *bitbuffer)
{
  //EXPERIMENT
  // This works
  fprintf(stderr, "\nBefore data1\n");
  data_t *data1;
  data1 = data_make("foo1", "FOO1", DATA_DOUBLE, 42.0,
                    NULL);
  data_acquired_handler(data1);
  
  // This also works
  fprintf(stderr, "\nBefore data2\n");
  data_t *data2;
  data2 = data_make("bar2", "BAR2", DATA_STRING, "I am Bar2",
                    "more2", "MORE2", DATA_DATA, data_make("foo2", "FOO2", DATA_DOUBLE, 42.0,
                                                         NULL),
                    NULL);
  data_acquired_handler(data2);
  
  //But this seems to produce an infinite loop / segmentation fault
  fprintf(stderr, "\nBefore data3\n");
  data_t *data3;
  data3 = data_make("bar3", "BAR3", DATA_STRING, "I am Bar3",
                    "more3", "MORE3", DATA_DATA, data1,
                    NULL);
  data_acquired_handler(data3);
  
  fprintf(stderr, "\nBefore return\n");
  return 0;
}

