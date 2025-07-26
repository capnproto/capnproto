;;; capnp-mode.el --- Major mode for editing Capn' Proto Files -*- lexical-binding: t; -*-

;; This is free and unencumbered software released into the public domain.

;; Author: Brian Taylor <el.wubo@gmail.com>
;; Author: Rohit Goswami (HaoZeke) <rgoswami[at]inventati[dot]org>
;; Version: 1.1.0
;; Keywords: languages, faces
;; URL: https://github.com/capnproto/capnproto
;; Package-Requires: ((emacs "24.3"))

;;; Commentary:

;; Provides basic syntax highlighting for capnp files.
;;
;; To use:
;;
;; Add something like this to your .emacs file:
;;
;; (add-to-list 'load-path "~/src/capnproto/highlighting/emacs")
;; (require 'capnp-mode)
;;

;;; Code:

;; command to comment/uncomment text
(defun capnp-comment-dwim (arg)
  "Comment or uncomment current line or region in a smart way.
For detail, see `comment-dwim' for ARG explanation."
  (interactive "*P")
  (require 'newcomment)
  (let (
        (comment-start "#") (comment-end ""))
    (comment-dwim arg)))

;; Define keywords for Cap'n Proto
(defvar capnp-keywords
  '("struct" "enum" "interface" "union" "import"
    "using" "const" "annotation" "extends" "in"
    "of" "on" "as" "with" "from" "fixed"))

;; Define built-in types for Cap'n Proto
(defvar capnp-builtins
  '("union" "group" "Void" "Bool" "Int8" "Int16"
    "Int32" "Int64" "UInt8" "UInt16" "UInt32"
    "UInt64" "Float32" "Float64" "Text" "Data"
    "AnyPointer" "AnyStruct" "Capability" "List"))

;; Define constants (e.g., boolean literals) for Cap'n Proto
(defvar capnp-constants
  '("true" "false" "inf"))

;; Define regex for comments (single line starting with #)
(defvar capnp-comment-regexp "#.*$")

;; Define regex for types (starting with a letter or underscore)
(defvar capnp-type-regexp "\\_<\\([A-Za-z_][A-Za-z0-9_]*\\)\\_>")

;; Define regex for numbers
(defvar capnp-number-regexp "\\_<[0-9]+\\_>")

;; Define regex for floating-point numbers
(defvar capnp-float-regexp "\\_<[0-9]+\\.[0-9]*\\([eE][-+]?[0-9]+\\)?\\_>")

;; Define regex for Cap'n Proto unique IDs (e.g., @0xbd1f89fa17369103)
(defvar capnp-unique-id-regexp "@0x[0-9A-Fa-f]+\\b")

;; Extend keywords to include annotation-related targets
(defvar capnp-annotation-targets
  '("file" "struct" "field" "union" "group" "enum" "enumerant" "interface" "method" "param" "annotation" "const" "*"))

;; Define regex for annotations (e.g., $foo("bar"))
(defvar capnp-annotation-regexp "\\([$]\\w+\\)(\\([^)]+\\))?")

;; Define syntax table to manage comments
(defvar capnp-mode-syntax-table
  (let ((table (make-syntax-table)))
    ;; '#' starts a comment
    (modify-syntax-entry ?# "<" table)
    (modify-syntax-entry ?\n ">" table)
    table))

;; Define font lock (syntax highlighting) rules
(defvar capnp-font-lock-keywords
  `((,(regexp-opt capnp-keywords 'words) . font-lock-keyword-face)
    (,(regexp-opt capnp-builtins 'words) . font-lock-type-face)
    (,(regexp-opt capnp-constants 'words) . font-lock-constant-face)
    (,capnp-type-regexp . font-lock-variable-name-face)
    (,capnp-number-regexp . font-lock-constant-face)
    (,capnp-float-regexp . font-lock-constant-face)
    (,capnp-unique-id-regexp . font-lock-constant-face)
    (,capnp-comment-regexp . font-lock-comment-face)
    (,capnp-annotation-regexp . ((1 font-lock-preprocessor-face) (2 font-lock-string-face)))
    (,(regexp-opt capnp-annotation-targets 'words) . font-lock-builtin-face)))

;; Define the major mode itself
(define-derived-mode capnp-mode c-mode "Cap'n Proto"
  "Major mode for editing Cap'n Proto schema files."
  :syntax-table capnp-mode-syntax-table
  (setq-local font-lock-defaults '((capnp-font-lock-keywords)))
  (setq-local comment-start "# ")
  (setq-local comment-end "")
  (setq-local indent-line-function 'c-indent-line)
  (define-key capnp-mode-map [remap comment-dwim] 'capnp-comment-dwim))

;; Automatically use capnp-mode for .capnp files
(add-to-list 'auto-mode-alist '("\\.capnp\\'" . capnp-mode))

(provide 'capnp-mode)
;;; capnp-mode.el ends here
