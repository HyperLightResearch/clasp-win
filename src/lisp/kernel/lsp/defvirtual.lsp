(in-package :core)

(defmacro define-single-dispatch-generic-function (name lambda-list)
  `(core:ensure-single-dispatch-generic-function ',name ,lambda-list))

(defmacro defvirtual (name &rest args)
  (let ((lambda-list (if args
                         (pop args)
                         (error "Illegal defvirtual form: missing lambda list")))
        (body args))
    ;;    (print (list "lambda-list" lambda-list))
    (multiple-value-bind (simple-lambda-list dispatch-symbol dispatch-class dispatch-index)
        (core:process-single-dispatch-lambda-list lambda-list)
      (declare (ignore dispatch-symbol dispatch-index))
      (multiple-value-bind (declares code docstring specials)
          (core:process-declarations body t)
        (declare (ignore specials))
        (let* ((fn `(lambda ,simple-lambda-list ,@code)))
          `(core:ensure-single-dispatch-method (fdefinition ',name)
                                               ',name
                                               (find-class ',dispatch-class)
                                               :lambda-list-handler (make-lambda-list-handler
                                                                     ',simple-lambda-list
                                                                     ',declares 'function)
                                               :docstring ,docstring
                                               :body ,fn))))))
(export '(define-single-dispatch-generic-function defvirtual))
