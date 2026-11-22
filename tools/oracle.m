%% oracle.m -- CSV oracle for the fz library; runs under GNU Octave AND MATLAB.
%%
%% Octave (fuzzy-logic-toolkit on the path):
%%   octave --no-gui --quiet --eval "oracle('M.fis','in.txt',101,'out.csv','detail.json')"
%%   tools/run_oracle.sh MODEL.fis INPUTS.txt [NUM_POINTS] [OUT.csv] [DETAIL.json]
%%
%% MATLAB (Fuzzy Logic Toolbox):
%%   matlab -batch "addpath('<repo>/tools'); oracle('M.fis','in.txt',101,'out.csv','detail.json')"
%%
%% inputs   : one row per line, whitespace or comma separated, '#' starts a comment.
%% OUT.csv  : one row per input row, one column per FIS output, '%.17g'.
%%            A row the toolbox rejects prints 'error:<message>' so the caller
%%            records a failure instead of silently losing a row.
%% DETAIL   : JSON for the LAST input row -- rule_input, firing strength,
%%            rule_output, aggregated output -- with the same field names as
%%            `fis2json.py --details`, so the two can be compared numerically.
%%
%% Every number in tests/golden/ came out of this file; nothing is hand-written.

function oracle (fis_path, input_path, num_points, out_path, detail_path)

  if (nargin < 2 || nargin > 5)
    error ('oracle: usage oracle(fis_path, input_path[, num_points[, out_path[, detail_path]]])');
  end
  if (nargin < 3 || isempty (num_points))
    num_points = 101;
  end
  if (nargin < 4)
    out_path = '';
  end
  if (nargin < 5)
    detail_path = '';
  end

  % Under Octave the toolkit ships as loose .m files; put it first on the path
  % so its readfis/evalfis win.  MATLAB gets them from the Fuzzy Logic Toolbox.
  if (exist ('OCTAVE_VERSION', 'builtin'))
    toolkit = getenv ('FLT_PATH');
    if (isempty (toolkit))
      toolkit = '/var/tmp/flt-src/inst';
    end
    if (exist (toolkit, 'dir'))
      addpath (toolkit);
      addpath (fullfile (toolkit, 'private'));
    end
    if (exist ('readfis') ~= 2)
      error ('oracle: readfis not found -- is the fuzzy-logic-toolkit on the path?');
    end
  end

  fis = readfis (fis_path);
  inputs = read_input_rows (input_path, numel (fis.input));

  % one call per row so a rejected row does not abort the whole run
  lines_ = cell (size (inputs, 1), 1);
  for i = 1 : size (inputs, 1)
    try
      out = evalfis (inputs(i, :), fis, num_points);
      lines_{i} = sprintf ('%.17g,', out);
      lines_{i} = lines_{i}(1:end-1);
    catch err
      lines_{i} = ['error:' strrep(err.message, sprintf ('\n'), ' ')];
    end
  end

  if (~isempty (out_path))
    fid = fopen (out_path, 'w');
    if (fid < 0)
      error ('oracle: cannot write %s', out_path);
    end
    fprintf (fid, '%s\n', lines_{:});
    fclose (fid);
  else
    fprintf (1, '%s\n', lines_{:});
  end

  if (~isempty (detail_path))
    write_detail (fis, inputs(end, :), num_points, detail_path);
  end

end

%% ---------------------------------------------------------------------------

function inputs = read_input_rows (path, n_inputs)

  fid = fopen (path, 'r');
  if (fid < 0)
    error ('oracle: cannot read %s', path);
  end
  inputs = [];
  line_no = 0;
  while (true)
    line = fgetl (fid);
    if (~ischar (line))
      break;
    end
    line_no = line_no + 1;
    comment = strfind (line, '#');
    if (~isempty (comment))
      line = line(1:comment(1)-1);
    end
    line = strrep (line, ',', ' ');
    values = sscanf (line, '%f');
    if (isempty (values))
      continue;
    end
    if (numel (values) ~= n_inputs)
      fclose (fid);
      error ('oracle: %s:%d has %d values, the FIS has %d inputs', ...
             path, line_no, numel (values), n_inputs);
    end
    inputs = [inputs; values(:)'];
  end
  fclose (fid);

  if (isempty (inputs))
    error ('oracle: no input rows in %s', path);
  end

end

%% ---------------------------------------------------------------------------

function write_detail (fis, x, num_points, path)

  [out, rule_input, rule_output, fuzzy_output] = evalfis (x, fis, num_points);
  fid = fopen (path, 'w');
  if (fid < 0)
    error ('oracle: cannot write %s', path);
  end

  fprintf (fid, '{\n');
  fprintf (fid, '  "source": "tools/oracle.m",\n');
  fprintf (fid, '  "engine": "%s",\n', engine_name ());
  fprintf (fid, '  "type": "%s",\n', fis.type);
  fprintf (fid, '  "n_points": %d,\n', num_points);
  fprintf (fid, '  "inputs": %s,\n', json_row (x));
  fprintf (fid, '  "output": %s,\n', json_row (out));
  fprintf (fid, '  "rule_input": %s,\n', json_matrix (rule_input));
  % evalfis does not return the firing strength, so recompute it from the
  % rule_input it did return (same left-to-right t-norm fold the toolkit uses).
  fprintf (fid, '  "firing": %s,\n', json_row (firing_strength (fis, rule_input)));
  fprintf (fid, '  "rule_output": %s,\n', json_matrix (rule_output));

  if (strcmp (fis.type, 'mamdani'))
    % num_points x num_outputs, one column per output variable
    fprintf (fid, '  "fuzzy_output": %s\n', json_matrix (fuzzy_output));
  else
    % vector of structs; each holds a 2xL (location, height) matrix
    parts = cell (1, numel (fuzzy_output));
    for i = 1 : numel (fuzzy_output)
      parts{i} = json_matrix (fuzzy_output(i).aggregated_output);
    end
    fprintf (fid, '  "singletons": [%s]\n', strjoin (parts, ', '));
  end
  fprintf (fid, '}\n');
  fclose (fid);

end

%% ---------------------------------------------------------------------------

function s = engine_name ()
  if (exist ('OCTAVE_VERSION', 'builtin'))
    s = ['GNU Octave ' version() ' + fuzzy-logic-toolkit'];
  else
    v = ver ('fuzzy');
    if (isempty (v))
      s = ['MATLAB ' version()];
    else
      s = ['MATLAB ' version() ' + ' v(1).Name ' ' v(1).Version];
    end
  end
end

function f = firing_strength (fis, rule_input)
  % Mirror of the toolkit's eval_firing_strength; evalfis does not return it.
  f = zeros (1, numel (fis.rule));
  for i = 1 : numel (fis.rule)
    rule = fis.rule(i);
    mus = [];
    for j = 1 : numel (fis.input)
      if (rule.antecedent(j) ~= 0)
        mus = [mus rule_input(i, j)];
      end
    end
    if (rule.connection == 1)
      connect = fis.andMethod;
    else
      connect = fis.orMethod;
    end
    switch (connect)
      case 'min'
        f(i) = rule.weight * min (mus);
      case 'max'
        f(i) = rule.weight * max (mus);
      case {'prod', 'algebraic_product'}
        f(i) = rule.weight * prod (mus);
      case 'sum'
        f(i) = rule.weight * sum (mus);
      case {'probor', 'algebraic_sum'}
        acc = 0;
        for k = 1 : numel (mus)
          acc = acc + mus(k) - acc * mus(k);
        end
        f(i) = rule.weight * acc;
      case 'einstein_product'
        acc = 1;
        for k = 1 : numel (mus)
          y = mus(k);
          acc = (acc * y) / (2 - (acc + y - acc * y));
        end
        f(i) = rule.weight * acc;
      case 'einstein_sum'
        acc = 0;
        for k = 1 : numel (mus)
          y = mus(k);
          acc = (acc + y) / (1 + acc * y);
        end
        f(i) = rule.weight * acc;
      case 'hamacher_product'
        acc = 1;
        for k = 1 : numel (mus)
          y = mus(k);
          if (acc == 0 && y == 0)
            acc = 0;
          else
            acc = (acc * y) / (acc + y - acc * y);
          end
        end
        f(i) = rule.weight * acc;
      case 'hamacher_sum'
        acc = 0;
        for k = 1 : numel (mus)
          y = mus(k);
          if (acc == 1 && y == 1)
            acc = 1;
          else
            acc = (acc + y - 2 * acc * y) / (1 - acc * y);
          end
        end
        f(i) = rule.weight * acc;
      case 'bounded_sum'
        acc = 0;
        for k = 1 : numel (mus)
          acc = min (1, acc + mus(k));
        end
        f(i) = rule.weight * acc;
      otherwise
        f(i) = NaN;
    end
  end
end

function s = json_row (row)
  s = '[';
  for i = 1 : numel (row)
    if (i > 1)
      s = [s ', '];
    end
    s = [s num2str17(row(i))];
  end
  s = [s ']'];
end

function s = json_matrix (m)
  if (isempty (m))
    s = '[]';
    return;
  end
  s = '[';
  for i = 1 : size (m, 1)
    if (i > 1)
      s = [s ', '];
    end
    s = [s json_row(m(i, :))];
  end
  s = [s ']'];
end

function s = num2str17 (v)
  s = sprintf ('%.17g', v);
  if (any (strcmp (s, {'NaN', '-NaN', 'Inf', '-Inf'})))
    s = lower (s);
  end
end
