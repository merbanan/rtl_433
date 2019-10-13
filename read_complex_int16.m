function v = read_complex_int16(filename)
%% usage: read_complex_int16 (filename, [count])
%%
%%  open filename and return the contents as a column vector,
%%  reading 8bit unsigned, and treating them as doubles (and normalizing
%% to (+/- 1)
%%

% if ((m = nargchk (1,2,nargin)))
%   usage (m);
% endif;

f = fopen (filename, 'rb');
if (f < 0)
v = 0;
else
v = fread (f, [1, Inf], 'int16=>double');
fclose (f);
end;
