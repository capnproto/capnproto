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
            (r'@[0-9a-zA-Z]*', Name.Decorator),
            (r'=', Literal, 'expression'),
            (r':', Name.Class, 'type'),
            (r'\$', Name.Attribute, 'annotation'),
            (r'(struct|enum|interface|union|import|using|const|annotation|extends|in|of|on|as|with|from|fixed|bulk|realtime)\b',
                Token.Keyword),
            (r'[a-zA-Z0-9_.]+', Token.Name),
            (r'[^#@=:$a-zA-Z0-9_]+', Text),
        ],
        'type': [
            (r'[^][=;,(){}$]+', Name.Class),
            (r'[[(]', Name.Class, 'parentype'),
            (r'', Name.Class, '#pop')
        ],
        'parentype': [
            (r'[^][;()]+', Name.Class),
            (r'[[(]', Name.Class, '#push'),
            (r'[])]', Name.Class, '#pop'),
            (r'', Name.Class, '#pop')
        ],
        'expression': [
            (r'[^][;,(){}$]+', Literal),
            (r'[[(]', Literal, 'parenexp'),
            (r'', Literal, '#pop')
        ],
        'parenexp': [
            (r'[^][;()]+', Literal),
            (r'[[(]', Literal, '#push'),
            (r'[])]', Literal, '#pop'),
            (r'', Literal, '#pop')
        ],
        'annotation': [
            (r'[^][;,(){}=:]+', Name.Attribute),
            (r'[[(]', Name.Attribute, 'annexp'),
            (r'', Name.Attribute, '#pop')
        ],
        'annexp': [
            (r'[^][;()]+', Name.Attribute),
            (r'[[(]', Name.Attribute, '#push'),
            (r'[])]', Name.Attribute, '#pop'),
            (r'', Name.Attribute, '#pop')
        ],
    }

if __name__ == "__main__":
    from setuptools import setup, find_packages
    setup(name = "CapnpPygmentsLexer",
          version = "0.1",
          packages = find_packages(),
          py_modules = [ 'capnp_lexer' ],
          entry_points = {'pygments.lexers': 'capnp = capnp_lexer:CapnpLexer'})
