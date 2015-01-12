Syntax Coloring for Emacs
=========================

How to use:

Add this to your .emacs file (altering the path to wherever your
capnproto directory lives):

```elisp
(add-to-list 'load-path "~/src/capnproto/highlighting/emacs")
(require 'capnp-mode)
(add-to-list 'auto-mode-alist '("\\.capnp\\'" . capnp-mode))
```
