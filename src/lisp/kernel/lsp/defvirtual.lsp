(in-package :core)

(defmacro define-single-dispatch-generic-function (name lambda-list)
  `(core:ensure-single-dispatch-generic-function ',name ,lambda-list))

(defmacro defvirtual (name lambda-list &body body)
  (multiple-value-bind (simple-lambda-list dispatch-symbol dispatch-class dispatch-index)
      (core:process-single-dispatch-lambda-list lambda-list)
    (declare (ignore dispatch-symbol dispatch-index))
    (multiple-value-bind (declares code docstring specials)
        (core:process-declarations body t)
      (declare (ignore specials))
      (let* ((fn `(lambda ,simple-lambda-list
                    (flet ((call-next-method (&rest args)
                             (error "In defvirtual defined call-next-method implement a proper one")))
                      ,@code))))
        `(core:ensure-single-dispatch-method (fdefinition ',name)
                                             ',name
                                             (find-class ',dispatch-class)
                                             :lambda-list-handler (make-lambda-list-handler
                                                                   ',simple-lambda-list
                                                                   ',declares 'function)
                                             :docstring ,docstring
                                             :body ,fn)))))
(export '(define-single-dispatch-generic-function defvirtual))
