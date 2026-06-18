% example.m - psy_parallel MEX demo (MATLAB / Octave)
%
% Build first:   run build.m   (produces psy_parallel_mex.<mexext>)
% Then run this script. On Linux see the top-level README for ppdev access.

code = 42;

ports = psy_parallel_mex('list');
fprintf('%d parallel port(s) detected\n', numel(ports));
for i = 1:numel(ports)
    fprintf('  %s (backend %d, base 0x%X)\n', ...
            ports(i).name, ports(i).backend, ports(i).base_addr);
end

% Platform defaults: ppdev /dev/parport0 (Linux), inpout @ LPT1 (Windows).
% For raw x86 I/O:   h = psy_parallel_mex('open', 'direct', 888);
% For a specific device: h = psy_parallel_mex('open', '/dev/parport1');
h = psy_parallel_mex('open');
cleanup = onCleanup(@() psy_parallel_mex('close', h));   %#ok<NASGU>

psy_parallel_mex('pulse', h, code, 2000);      % blocking 2 ms trigger
fprintf('sent trigger %d (2 ms blocking pulse)\n', code);

psy_parallel_mex('pulseasync', h, code, 2000); % non-blocking; worker drops it
fprintf('queued async trigger %d\n', code);

s = psy_parallel_mex('status', h);
fprintf('status = %d\n', s);
