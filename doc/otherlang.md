---
layout: page
---

# Other Languages

Currently, Cap'n Proto is implemented only in C++.  We'd like to support many more languages in
the future!

If you'd like to own the implementation of Cap'n Proto in some particular language,
[let us know](https://groups.google.com/group/capnproto)!

**You should e-mail the list _before_ you start hacking.**  We don't bite, and we'll probably have
useful tips that will save you time.  :)

**Do not implement your own schema parser.**  The schema language is more compilcated than it
looks, and the algorithm to determine offsets of fields is subtle.  If you reuse `capnpc`'s parser,
you won't risk getting these wrong, and you won't have to spend time keeping your parser up-to-date.
It will soon be possible to write code generator "plugins" for `capnpc` in any language, not just
Haskell.  (A guide will be added here when this is implemented.)
