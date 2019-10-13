demod_data = read_complex_int16('build/fsk_demod_s16');
demod_min = read_complex_int16('build/fsk_min_track_s16');
demod_max = read_complex_int16('build/fsk_max_track_s16');
demod_mid = read_complex_int16('build/fsk_mid_track_s16');

len = 1:length(demod_data);
plot(len,demod_data,len,demod_max,len,demod_min,len,demod_mid)