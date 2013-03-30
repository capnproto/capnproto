#! /usr/bin/env python

from pygments.lexer import RegexLexer
from pygments.token import *

class CapnpLexer(RegexLexer):
    name = "Cap'n Proto lexer"
    aliases = ['capnp']
    filenames = ['*.capnp']

    tokens = {
        'root': [
            (r'#.*?$', Comment.Single),
            (r'@[0-9]*', Name.Decorator),
            (r'=[^;]*', Literal),
            (r':[^;=]*', Name.Class),
            (r'@[0-9]*', Token.Annotation),
            (r'(struct|enum|interface|union|import|using|const|option|in|of|on|as|with|from)\b',
                Token.Keyword),
            (r'[a-zA-Z0-9_.]+', Token.Name),
            (r'[^#@=:a-zA-Z0-9_]+', Text),
        ]
    }

if __name__ == "__main__":
    from setuptools import setup, find_packages
    setup(name = "CapnpPygmentsLexer",
          version = "0.1",
          packages = find_packages(),
          py_modules = [ 'capnp_lexer' ],
          entry_points = {'pygments.lexers': 'capnp = capnp_lexer:CapnpLexer'})
