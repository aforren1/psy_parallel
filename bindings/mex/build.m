function build()
%BUILD  Compile the psy_parallel MEX function (MATLAB or Octave).
%
%   Run this from any directory:  run('.../bindings/mex/build.m')
%   It produces psy_parallel_mex.<mexext> next to the source.
%
%   Octave needs the development package (e.g. `liboctave-dev`) for `mex`.
%   MATLAB needs a configured C compiler (`mex -setup`).

    here = fileparts(mfilename('fullpath'));
    root = fullfile(here, '..', '..');           % repo root holds psy_parallel.h
    src  = fullfile(here, 'psy_parallel_mex.c');

    args = {['-I' root], src};
    if ~ispc
        % The async-pulse worker uses pthreads on Linux/macOS. On Windows it
        % uses Win32 threads, so no extra link library is needed there.
        args{end+1} = '-lpthread';
    end

    mex(args{:});
    fprintf('built psy_parallel_mex (%s)\n', mexext);
end
