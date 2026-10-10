# MariaDB REST Service (MRS) ANTLR Grammar

The REST SQL statements are run by the `mrs` module of MariaDB Shell, whose
parser is the bison grammar `../core/mrs_parser.yy`. This ANTLR4 grammar
(`MRSLexer.g4`, `MRSParser.g4`) is the **reference grammar of the
documentation**. The two grammars are kept rule for rule in step: a statement
change goes to both.

The ANTLR4 grammar syntax support VS Code extension
(`mike-lischke.vscode-antlr4`) helps with writing and debugging the grammar.

## Documentation Tools

The REST SQL reference lives in
`docs-ref/content/mariadb-rest-service/rest-sql-reference/` (GitBook source).
Two scripts in `scripts/` keep it in step with the grammar. Both need only the
Python standard library; run them from the repository root after every
grammar change:

| npm script | What it does |
| --- | --- |
| `npm run docs-ref:mrs-grammar-docs` | `scripts/update_grammar_docs.py`: checks the ```` ```antlr ```` rule blocks of the reference pages against `MRSParser.g4`, updates outdated ones and lists rules not documented yet (`--print-missing` prints them ready to paste). |
| `npm run docs-ref:mrs-diagrams` | `scripts/generate_rrd_svg_files.py --prune`: renders one railroad diagram per parser rule to `docs-ref/content/.gitbook/assets/mariadb-rest-service/sql/<rule>.svg` and deletes the diagrams of removed rules. `--check` only reports changes. |

The statements of the grammar are tested by the shell's
`unittest/scripts/auto/py_shell/scripts/mrs_grammar_test_norecord.py`, which
runs `unittest/data/mrs/grammar_test.sql`.

Copyright (c) 2023, Oracle and/or its affiliates.
Copyright (c) 2026, MariaDB plc.
