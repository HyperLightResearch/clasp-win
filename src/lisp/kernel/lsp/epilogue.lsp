#+clasp-min(eval-when (:load-toplevel)
           (process-command-line-load-eval-sequence)
           (core::select-package :core)
           (let ((core:*use-interpreter-for-eval* nil))
             (when (member :interactive *features*) (core::low-level-repl))))
#+bclasp(eval-when (:load-toplevel)
          (cl:in-package :cl-user)
          (process-command-line-load-eval-sequence)
          (let ((core:*use-interpreter-for-eval* nil))
            (when (member :interactive *features*) (core:run-repl))))
#+cclasp(eval-when (:load-toplevel)
          (cl:in-package :cl-user)
          (core:process-extension-loads)
          (core:load-clasprc)
          (core:process-command-line-load-eval-sequence)
          (let ((core:*use-interpreter-for-eval* nil))
            (when (member :interactive *features*) (core:top-level))))
