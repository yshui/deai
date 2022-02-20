# -*- coding: utf-8 -*-
"""
    sphinx.domains.lua
    ~~~~~~~~~~~~~~~~~~~~~

    The lua domain.

    :copyright: Copyright 2007-2017 by the Sphinx team, see AUTHORS.
    :license: BSD, see LICENSE for details.
"""

import re
from docutils import nodes
from docutils.parsers.rst import Directive, directives
from sphinx import addnodes
from sphinx.roles import XRefRole
from sphinx.domains import Domain, ObjType, Index
from sphinx.directives import ObjectDescription
from sphinx.locale import get_translation
from sphinx.util import logging
from sphinx.util.nodes import make_refnode
from sphinx.util.docfields import Field, GroupedField, TypedField
from sphinx.environment import BuildEnvironment
from sphinx.builders import Builder
from typing import Any, Dict, Iterable, Iterator, List, Tuple, Optional, Union

MESSAGE_CATALOG_NAME = 'sphinx-luadomain'
_ = get_translation(MESSAGE_CATALOG_NAME)

logger = logging.getLogger(__name__)

# REs for Lua signatures
lua_sig_re = re.compile(
    r'''^ ([\w.]*\.)?            # class name(s)
          (\w+)  \s*             # thing name
          (?: \((.*)\)           # optional: arguments
           (?:\s* -> \s* (.*))?  #           return annotation
          )? $                   # and nothing more
          ''', re.VERBOSE)


def _pseudo_parse_arglist(sig_node: addnodes.desc_signature, arg_list: str) -> None:
    """"Parse" a list of arguments separated by commas.

    Arguments can have "optional" annotations given by enclosing them in
    brackets.  Currently, this will split at any comma, even if it's inside a
    string literal (e.g. default argument value).
    """
    param_list = addnodes.desc_parameterlist()
    stack = [param_list]
    try:
        for argument in arg_list.split(','):
            argument = argument.strip()
            ends_open = ends_close = 0
            while argument.startswith('['):
                stack.append(addnodes.desc_optional())
                stack[-2] += stack[-1]
                argument = argument[1:].strip()
            while argument.startswith(']'):
                stack.pop()
                argument = argument[1:].strip()
            while argument.endswith(']') and not argument.endswith('[]'):
                ends_close += 1
                argument = argument[:-1].strip()
            while argument.endswith('['):
                ends_open += 1
                argument = argument[:-1].strip()
            if argument:
                stack[-1] += addnodes.desc_parameter(argument, argument)
            while ends_open:
                stack.append(addnodes.desc_optional())
                stack[-2] += stack[-1]
                ends_open -= 1
            while ends_close:
                stack.pop()
                ends_close -= 1
        if len(stack) != 1:
            raise IndexError
    except IndexError:
        # if there are too few or too many elements on the stack, just give up
        # and treat the whole argument list as one argument, discarding the
        # already partially populated paramlist node
        sig_node += addnodes.desc_parameterlist()
        sig_node[-1] += addnodes.desc_parameter(arg_list, arg_list)
    else:
        sig_node += param_list


class LuaField(Field):
    pass


class LuaGroupedField(GroupedField):
    pass


class LuaTypedField(TypedField):
    pass


class LuaObject(ObjectDescription):
    """
    Description of a general Lua object.

    :cvar allow_nesting: Class is an object that allows for nested namespaces
    :vartype allow_nesting: bool
    """
    option_spec = {
        'noindex': directives.flag,
        'module': directives.unchanged,
        'annotation': directives.unchanged,
        'virtual': directives.flag,
        'protected': directives.flag,
        'abstract': directives.flag,
        'deprecated': directives.flag,
    }

    doc_field_types = [
        LuaTypedField('parameter', label=_('Parameters'),
                      names=('param', 'parameter', 'arg', 'argument',
                             'keyword', 'kwarg', 'kwparam'),
                      typerolename=None, typenames=('paramtype', 'type'),
                      can_collapse=True),
        Field('returnvalue', label=_('Returns'), has_arg=False,
              names=('returns', 'return')),
        LuaField('returntype', label=_('Return type'), has_arg=False,
                 names=('rtype',), bodyrolename=None),
    ]

    allow_nesting = False

    def get_signature_prefix(self, signature: str) -> str:
        """May return a prefix to put before the object name in the
        signature.
        """
        prefix = []

        if 'virtual' in self.options:
            prefix.append('virtual')
        if 'protected' in self.options:
            prefix.append('protected')
        if 'abstract' in self.options:
            prefix.append('abstract')
        return ' '.join(prefix) + ' '

    def needs_arg_list(self) -> bool:
        """May return true if an empty argument list is to be generated even if
        the document contains none.
        """
        return False

    def handle_signature(self, sig: str, sig_node: addnodes.desc_signature) -> Tuple[str, str]:
        """Transform a Lua signature into RST nodes.

        Return (fully qualified name of the thing, classname if any).

        If inside a class, the current class name is handled intelligently:
        * it is stripped from the displayed name if present
        * it is added to the full name (return value) if not present
        """
        m = lua_sig_re.match(sig)
        if m is None:
            raise ValueError
        name_prefix, name, arg_list, ret_ann = m.groups()

        # determine module and class name (if applicable), as well as full name
        modname = self.options.get(
            'module', self.env.ref_context.get('lua:module'))
        class_name = self.env.ref_context.get('lua:class')
        if class_name:
            add_module = False
            if name_prefix and name_prefix.startswith(class_name):
                fullname = name_prefix + name
                # class name is given again in the signature
                name_prefix = name_prefix[len(class_name):].lstrip('.')
            elif name_prefix:
                # class name is given in the signature, but different
                # (shouldn't happen)
                fullname = class_name + '.' + name_prefix + name
            else:
                # class name is not given in the signature
                fullname = class_name + '.' + name
        else:
            add_module = True
            if name_prefix:
                class_name = name_prefix.rstrip('.')
                fullname = name_prefix + name
            else:
                class_name = ''
                fullname = name

        sig_node['module'] = modname
        sig_node['class'] = class_name
        sig_node['fullname'] = fullname

        sig_prefix = self.get_signature_prefix(sig)
        if sig_prefix:
            sig_node += addnodes.desc_annotation(sig_prefix, sig_prefix)

        if name_prefix:
            sig_node += addnodes.desc_addname(name_prefix, name_prefix)
        elif add_module and self.env.config.add_module_names:
            modname = self.options.get(
                'module', self.env.ref_context.get('lua:module'))
            if modname:
                node_text = modname + '.'
                sig_node += addnodes.desc_addname(node_text, node_text)

        annotation = self.options.get('annotation')

        sig_node += addnodes.desc_name(name, name)
        if not arg_list:
            if self.needs_arg_list():
                # for callables, add an empty parameter list
                sig_node += addnodes.desc_parameterlist()
            if ret_ann:
                sig_node += addnodes.desc_returns(ret_ann, ret_ann)
            if annotation:
                sig_node += addnodes.desc_annotation(' ' + annotation, ' ' + annotation)
            return fullname, name_prefix

        _pseudo_parse_arglist(sig_node, arg_list)
        if ret_ann:
            sig_node += addnodes.desc_returns(ret_ann, ret_ann)
        if annotation:
            sig_node += addnodes.desc_annotation(' ' + annotation, ' ' + annotation)
        return fullname, name_prefix

    def get_index_text(self, modname: str, name: str) -> str:
        """Return the text for the index entry of the object."""
        raise NotImplementedError('must be implemented in subclasses')

    def add_target_and_index(self, name_cls: str, sig: str, sig_node: addnodes.desc_signature) -> None:
        modname = self.options.get(
            'module', self.env.ref_context.get('lua:module'))
        fullname = (modname and modname + '.' or '') + name_cls[0]
        # note target

        if fullname not in self.state.document.ids:
            sig_node['names'].append(fullname)
            sig_node['ids'].append(fullname)
            sig_node['first'] = (not self.names)
            self.state.document.note_explicit_target(sig_node)
            objects = self.env.domaindata['lua']['objects']
            if fullname in objects:
                self.state_machine.reporter.warning(
                    'duplicate object description of %s, ' % fullname +
                    'other instance in ' +
                    self.env.doc2path(objects[fullname][0]) +
                    ', use :noindex: for one of them',
                    line=self.lineno)
            objects[fullname] = (self.env.docname, self.objtype)

        indextext = self.get_index_text(modname, name_cls)
        if indextext:
            self.indexnode['entries'].append(('single', indextext,
                                              fullname, '', None))

    def before_content(self) -> None:
        """Handle object nesting before content

        :lua:class:`LuaObject` represents Lua language constructs. For
        constructs that are nestable, such as a Lua classes, this method will
        build up a stack of the nesting heirarchy so that it can be later
        de-nested correctly, in :lua:meth:`after_content`.

        For constructs that aren't nestable, the stack is bypassed, and instead
        only the most recent object is tracked. This object prefix name will be
        removed with :lua:meth:`after_content`.
        """
        prefix = None
        if self.names:
            # fullname and name_prefix come from the `handle_signature` method.
            # fullname represents the full object name that is constructed using
            # object nesting and explicit prefixes. `name_prefix` is the
            # explicit prefix given in a signature
            (fullname, name_prefix) = self.names[-1]
            if self.allow_nesting:
                prefix = fullname
            elif name_prefix:
                prefix = name_prefix.strip('.')
        if prefix:
            self.env.ref_context['lua:class'] = prefix
            if self.allow_nesting:
                classes = self.env.ref_context.setdefault('lua:classes', [])
                classes.append(prefix)
        if 'module' in self.options:
            modules = self.env.ref_context.setdefault('lua:modules', [])
            modules.append(self.env.ref_context.get('lua:module'))
            self.env.ref_context['lua:module'] = self.options['module']

    def after_content(self) -> None:
        """Handle object de-nesting after content

        If this class is a nestable object, removing the last nested class prefix
        ends further nesting in the object.

        If this class is not a nestable object, the list of classes should not
        be altered as we didn't affect the nesting levels in
        :lua:meth:`before_content`.
        """
        classes = self.env.ref_context.setdefault('lua:classes', [])
        if self.allow_nesting:
            try:
                classes.pop()
            except IndexError:
                pass
        self.env.ref_context['lua:class'] = (classes[-1] if len(classes) > 0
                                             else None)
        if 'module' in self.options:
            modules = self.env.ref_context.setdefault('lua:modules', [])
            if modules:
                self.env.ref_context['lua:module'] = modules.pop()
            else:
                self.env.ref_context.pop('lua:module')


class LuaModuleLevel(LuaObject):
    """
    Description of an object on module level (functions, data).
    """

    def needs_arg_list(self) -> bool:
        return self.objtype == 'function'

    def get_index_text(self, modname: str, name_cls: str) -> str:
        if self.objtype == 'function':
            if not modname:
                return _('%s() (built-in function)') % name_cls[0]
            return _('%s() (in module %s)') % (name_cls[0], modname)
        elif self.objtype == 'data':
            if not modname:
                return _('%s (built-in variable)') % name_cls[0]
            return _('%s (in module %s)') % (name_cls[0], modname)
        else:
            return ''


class LuaClassLike(LuaObject):
    """
    Description of a class-like object (classes, interfaces).
    """

    allow_nesting = True

    CLASS_DEF_RE = re.compile(r'^\s*([\w.]*)(?:\s*:\s*(.*))?')

    def handle_signature(self, sig: str, sig_node: addnodes.desc_signature) -> Tuple[str, str]:
        """Transform a Lua signature into RST nodes.

        Return (fully qualified name of the thing, classname if any).

        If inside a class, the current class name is handled intelligently:
        * it is stripped from the displayed name if present
        * it is added to the full name (return value) if not present
        """
        m = self.CLASS_DEF_RE.match(sig)
        if m is None:
            raise ValueError

        class_name, base_classes_raw = m.groups()
        if base_classes_raw:
            base_classes = re.findall(r'[\w.]+', base_classes_raw)
        else:
            base_classes = []

        # determine module and class name (if applicable), as well as full name
        modname = self.options.get('module', self.env.ref_context.get('lua:module'))
        classname = self.env.ref_context.get('lua:class')

        sig_node['module'] = modname
        sig_node['class'] = classname
        sig_node['fullname'] = class_name

        sig_prefix = self.get_signature_prefix(sig)
        if sig_prefix:
            sig_node += addnodes.desc_annotation(sig_prefix, sig_prefix)

        modname = self.options.get('module', self.env.ref_context.get('lua:module'))
        if modname:
            nodetext = modname + '.'
            sig_node += addnodes.desc_addname(nodetext, nodetext)

        sig_node += addnodes.desc_name(class_name, class_name)
        sig_node += addnodes.desc_annotation(": ", ": ")

        for base in base_classes:
            p_node = addnodes.pending_xref(
                '', refdomain='lua', reftype='type',
                reftarget=base, modname=None, classname=None)
            p_node['lua:class'] = base
            p_node += nodes.Text(base)
            sig_node += p_node

            sig_node += nodes.Text(', ')
        sig_node.pop()

        return class_name, ''

    def add_target_and_index(self, name_cls: str, sig: str, sig_node: addnodes.desc_signature) -> None:
        modname = self.options.get('module', self.env.ref_context.get('lua:module'))
        fullname = (modname and modname + '.' or '') + name_cls[0]
        # note target

        if fullname not in self.state.document.ids:
            sig_node['names'].append(fullname)
            sig_node['ids'].append(fullname)
            sig_node['first'] = (not self.names)

            self.state.document.note_explicit_target(sig_node)
            objects = self.env.domaindata['lua']['objects']
            if fullname in objects:
                self.state_machine.reporter.warning(
                    'duplicate object description of %s, ' % fullname +
                    'other instance in ' +
                    self.env.doc2path(objects[fullname][0]) +
                    ', use :noindex: for one of them',
                    line=self.lineno)
            objects[fullname] = (self.env.docname, self.objtype)

        index_text = self.get_index_text(modname, name_cls)
        if index_text:
            self.indexnode['entries'].append(('single', index_text,
                                              fullname, '', None))

    def get_signature_prefix(self, signature: str) -> str:
        return self.objtype + ' '

    def get_index_text(self, modname: str, name_cls: str) -> str:
        if self.objtype == 'class':
            if not modname:
                return _('%s (built-in class)') % name_cls[0]
            return _('%s (class in %s)') % (name_cls[0], modname)
        elif self.objtype == 'exception':
            return name_cls[0]
        else:
            return ''


class LuaClassAttribute(LuaObject):
    """
    Description of a class attribute.
    """
    allow_nesting = True
    ATTRIBUTE_DEF_RE = re.compile(r'^\s*([\w.]*)(?:\s*:\s*(.*))?')

    def handle_signature(self, signature: str, sig_node: addnodes.desc_signature) -> Tuple[str, str]:
        m = self.ATTRIBUTE_DEF_RE.match(signature)
        if m is None:
            raise ValueError

        attr_name, attr_type = m.groups()

        # determine module and class name (if applicable), as well as full name
        modname = self.options.get('module', self.env.ref_context.get('lua:module'))
        classname = self.env.ref_context.get('lua:class')

        sig_node['module'] = modname
        sig_node['class'] = classname
        sig_node['fullname'] = attr_name

        sig_node += addnodes.desc_name(attr_name, attr_name)
        sig_node += addnodes.desc_annotation(": ", ": ")
        sig_node += addnodes.desc_type(attr_type, attr_type)

        return attr_name

    def add_target_and_index(self, name: str, sig: str, sig_node: addnodes.desc_signature) -> None:
        mod_name = self.options.get('module', self.env.ref_context.get('lua:module'))
        full_name = (mod_name and mod_name + '.' or '') + name

        if full_name not in self.state.document.ids:
            sig_node['names'].append(full_name)
            sig_node['ids'].append(full_name)
            sig_node['first'] = (not self.names)

            self.state.document.note_explicit_target(sig_node)
            objects = self.env.domaindata['lua']['objects']
            if full_name in objects:
                self.state_machine.reporter.warning(
                    'duplicate object description of %s, ' % full_name +
                    'other instance in ' +
                    self.env.doc2path(objects[full_name][0]) +
                    ', use :noindex: for one of them',
                    line=self.lineno)
            objects[full_name] = (self.env.docname, self.objtype)

        index_text = self.get_index_text(full_name)
        if index_text:
            self.indexnode['entries'].append(('single', index_text,
                                              full_name, '', None))

    def before_content(self):
        pass

    def after_content(self):
        pass

    def get_index_text(self, attr):
        return _('%s (attribute)') % attr


class LuaAliasObject(ObjectDescription):
    object_type = 'class'
    ALIAS_RE = re.compile(r'^ *([\w.]*) *= *(.*)$')

    def get_signature_prefix(self, signature: str) -> str:
        return 'alias '

    def handle_signature(self, signature: str, sig_node: addnodes.desc_signature) -> Tuple[str, str]:
        """Transform an alias declaration into RST nodes.
        .. lua:alias:: Bar = table<string, number>
        """
        m = LuaAliasObject.ALIAS_RE.match(signature)
        if m is None:
            raise ValueError
        alias, type_alias = m.groups()

        sig_node['alias'] = alias
        sig_node['type_alias'] = type_alias

        sig_prefix = self.get_signature_prefix(signature)
        sig_node += addnodes.desc_annotation(sig_prefix, sig_prefix)
        sig_node += addnodes.desc_name(alias, alias)
        sig_node += addnodes.desc_annotation(": ", ": ")
        sig_node += addnodes.desc_addname(type_alias, type_alias)

        return alias

    def add_target_and_index(self, alias_name: str, sig: str, sig_node: addnodes.desc_signature):
        if alias_name not in self.state.document.ids:
            sig_node['names'].append(alias_name)
            sig_node['ids'].append(alias_name)
            sig_node['first'] = (not self.names)
            self.state.document.note_explicit_target(sig_node)
            objects = self.env.domaindata['lua']['objects']
            if alias_name in objects:
                self.state_machine.reporter.warning(
                    'duplicate object description of %s, ' % alias_name +
                    'other instance in ' +
                    self.env.doc2path(objects[alias_name][0]) +
                    ', use :noindex: for one of them',
                    line=self.lineno)
            objects[alias_name] = (self.env.docname, self.object_type)

        index_text = self.get_index_text(alias_name)
        self.indexnode['entries'].append(('single', index_text,
                                          alias_name, '', None))

    def before_content(self):
        pass

    def after_content(self):
        pass

    def get_index_text(self, alias_name):
        return _('%s (alias)') % alias_name


class LuaClassMember(LuaObject):
    """
    Description of a class member (methods, attributes).
    """

    def needs_arg_list(self) -> bool:
        return self.objtype.endswith('method') or self.objtype == 'signal'

    def get_signature_prefix(self, signature: str) -> str:
        if self.objtype == 'staticmethod':
            return 'static '
        elif self.objtype == 'classmethod':
            return 'classmethod '
        elif self.objtype == 'signal':
            return 'signal '
        return super(LuaClassMember, self).get_signature_prefix(signature)

    def get_index_text(self, modname: str, name_cls: str) -> str:
        name, cls = name_cls
        add_modules = self.env.config.add_module_names
        if self.objtype == 'method' or self.objtype == 'signal':
            try:
                class_name, method_name = name.rsplit('.', 1)
            except ValueError:
                if modname:
                    return _('%s() (in module %s)') % (name, modname)
                else:
                    return '%s()' % name
            if modname and add_modules:
                return _('%s() (%s.%s method)') % (method_name, modname, class_name)
            else:
                return _('%s() (%s method)') % (method_name, class_name)
        elif self.objtype == 'staticmethod':
            try:
                class_name, method_name = name.rsplit('.', 1)
            except ValueError:
                if modname:
                    return _('%s() (in module %s)') % (name, modname)
                else:
                    return '%s()' % name
            if modname and add_modules:
                return _('%s() (%s.%s static method)') % (method_name, modname,
                                                          class_name)
            else:
                return _('%s() (%s static method)') % (method_name, class_name)
        elif self.objtype == 'classmethod':
            try:
                class_name, method_name = name.rsplit('.', 1)
            except ValueError:
                if modname:
                    return _('%s() (in module %s)') % (name, modname)
                else:
                    return '%s()' % name
            if modname:
                return _('%s() (%s.%s class method)') % (method_name, modname,
                                                         class_name)
            else:
                return _('%s() (%s class method)') % (method_name, class_name)
        elif self.objtype == 'attribute':
            try:
                class_name, attr_name = name.rsplit('.', 1)
            except ValueError:
                if modname:
                    return _('%s (in module %s)') % (name, modname)
                else:
                    return name
            if modname and add_modules:
                return _('%s (%s.%s attribute)') % (attr_name, modname, class_name)
            else:
                return _('%s (%s attribute)') % (attr_name, class_name)
        else:
            return ''


class LuaModule(Directive):
    """
    Directive to mark description of a new module.
    """

    has_content = False
    required_arguments = 1
    optional_arguments = 0
    final_argument_whitespace = False
    option_spec = {
        'platform': lambda x: x,
        'synopsis': lambda x: x,
        'noindex': directives.flag,
        'deprecated': directives.flag,
    }

    def run(self) -> List[nodes.Node]:
        env = self.state.document.settings.env
        modname = self.arguments[0].strip()
        no_index = 'noindex' in self.options
        env.ref_context['lua:module'] = modname
        ret = []
        if not no_index:
            env.domaindata['lua']['modules'][modname] = \
                (env.docname, self.options.get('synopsis', ''),
                 self.options.get('platform', ''), 'deprecated' in self.options)
            # make a duplicate entry in 'objects' to facilitate searching for
            # the module in LuaDomain.find_obj()
            env.domaindata['lua']['objects'][modname] = (env.docname, 'module')
            target_node = nodes.target('', '', ids=['module-' + modname],
                                      ismod=True)
            self.state.document.note_explicit_target(target_node)
            # the platform and synopsis aren't printed; in fact, they are only
            # used in the modindex currently
            ret.append(target_node)
            indextext = _('%s (module)') % modname
            inode = addnodes.index(entries=[('single', indextext,
                                             'module-' + modname, '', None)])
            ret.append(inode)
        return ret


class LuaCurrentModule(Directive):
    """
    This directive is just to tell Sphinx that we're documenting
    stuff in module foo, but links to module foo won't lead here.
    """

    has_content = False
    required_arguments = 1
    optional_arguments = 0
    final_argument_whitespace = False
    option_spec: Dict = {}

    def run(self) -> List[nodes.Node]:
        env = self.state.document.settings.env
        modname = self.arguments[0].strip()
        if modname == 'None':
            env.ref_context.pop('lua:module', None)
        else:
            env.ref_context['lua:module'] = modname
        return []


class LuaXRefRole(XRefRole):
    def process_link(self, env: BuildEnvironment, ref_node: nodes.Element, has_explicit_title: bool,
                     title: str, target: str) -> Tuple[str, str]:
        ref_node['lua:module'] = env.ref_context.get('lua:module')
        ref_node['lua:class'] = env.ref_context.get('lua:class')
        if not has_explicit_title:
            title = title.lstrip('.')  # only has a meaning for the target
            target = target.lstrip('~')  # only has a meaning for the title
            # if the first character is a tilde, don't display the module/class
            # parts of the contents
            if title[0:1] == '~':
                title = title[1:]
                dot = title.rfind('.')
                if dot != -1:
                    title = title[dot + 1:]
        return title, target


class LuaModuleIndex(Index):
    """
    Index subclass to provide the Lua module index.
    """

    name = 'modindex'
    localname = _('Lua Module Index')
    shortname = _('modules')

    def generate(self, docnames: Iterable[str] = None) -> Tuple[List[Tuple[str, List[List[Union[str, int]]]]], bool]:
        content: Dict[str, List] = {}
        # list of prefixes to ignore
        ignores = self.domain.env.config['modindex_common_prefix']
        ignores = sorted(ignores, key=len, reverse=True)
        # list of all modules, sorted by module name
        modules = sorted(self.domain.data['modules'].items(),
                         key=lambda x: x[0].lower())
        # sort out collapsable modules
        prev_modname = ''
        num_top_levels = 0
        for modname, (docname, synopsis, platforms, deprecated) in modules:
            if docnames and docname not in docnames:
                continue

            for ignore in ignores:
                if modname.startswith(ignore):
                    modname = modname[len(ignore):]
                    stripped = ignore
                    break
            else:
                stripped = ''

            # we stripped the whole module name?
            if not modname:
                modname, stripped = stripped, ''

            entries = content.setdefault(modname[0].lower(), [])

            package = modname.split('.')[0]
            if package != modname:
                # it's a submodule
                if prev_modname == package:
                    # first submodule - make parent a group head
                    if entries:
                        entries[-1][1] = 1
                elif not prev_modname.startswith(package):
                    # submodule without parent in list, add dummy entry
                    entries.append([stripped + package, 1, '', '', '', '', ''])
                subtype = 2
            else:
                num_top_levels += 1
                subtype = 0

            qualifier = deprecated and _('Deprecated') or ''
            entries.append([stripped + modname, subtype, docname,
                            'module-' + stripped + modname, platforms,
                            qualifier, synopsis])
            prev_modname = modname

        # apply heuristics when to collapse modindex at page load:
        # only collapse if number of toplevel modules is larger than
        # number of submodules
        collapse = len(modules) - num_top_levels < num_top_levels

        # sort by first letter
        sorted_content = sorted(content.items())

        return sorted_content, collapse


class LuaDomain(Domain):
    """Lua language domain."""
    name = 'lua'
    label = 'Lua'
    object_types: Dict[str, ObjType] = {
        'function': ObjType(_('function'), 'func', 'obj'),
        'data': ObjType(('data'), 'data', 'obj'),
        'class': ObjType(_('class'), 'class', 'exc', 'obj'),
        'alias': ObjType(_('alias'), 'alias', 'obj'),
        'exception': ObjType(_('exception'), 'exc', 'class', 'obj'),
        'method': ObjType(_('method'), 'meth', 'obj'),
        'classmethod': ObjType(_('class method'), 'meth', 'obj'),
        'staticmethod': ObjType(_('static method'), 'meth', 'obj'),
        'attribute': ObjType(_('attribute'), 'attr', 'obj'),
        'module': ObjType(_('module'), 'mod', 'obj'),
    }

    directives = {
        'function': LuaModuleLevel,
        'data': LuaModuleLevel,
        'class': LuaClassLike,
        'alias': LuaAliasObject,
        'exception': LuaClassLike,
        'method': LuaClassMember,
        'classmethod': LuaClassMember,
        'staticmethod': LuaClassMember,
        'signal': LuaClassMember,
        'attribute': LuaClassAttribute,
        'module': LuaModule,
        'currentmodule': LuaCurrentModule,
    }
    roles = {
        'data': LuaXRefRole(),
        'exc': LuaXRefRole(),
        'func': LuaXRefRole(fix_parens=True),
        'class': LuaXRefRole(),
        'alias': LuaXRefRole(),
        'const': LuaXRefRole(),
        'attr': LuaXRefRole(),
        'meth': LuaXRefRole(fix_parens=True),
        'mod': LuaXRefRole(),
        'obj': LuaXRefRole(),
        'sgnl': LuaXRefRole(fix_parens=True),
    }
    initial_data: Dict[str, Dict[str, Tuple[Any]]] = {
        'objects': {},  # fullname -> docname, objtype
        'modules': {},  # modname -> docname, synopsis, platform, deprecated
    }
    indices = [
        LuaModuleIndex,
    ]

    def clear_doc(self, doc_name: str) -> None:
        for fullname, (fn, _l) in list(self.data['objects'].items()):
            if fn == doc_name:
                del self.data['objects'][fullname]
        for modname, (fn, _x, _x, _x) in list(self.data['modules'].items()):
            if fn == doc_name:
                del self.data['modules'][modname]

    def merge_domaindata(self, doc_names: List[str], other_data: Dict) -> None:
        # XXX check duplicates?
        for fullname, (fn, objtype) in other_data['objects'].items():
            if fn in doc_names:
                self.data['objects'][fullname] = (fn, objtype)
        for modname, data in other_data['modules'].items():
            if data[0] in doc_names:
                self.data['modules'][modname] = data

    def find_obj(self, env: BuildEnvironment, modname: str, class_name: str, name: str, type: Optional[str],
                 search_mode: int = 0) -> List[Tuple[str, Any]]:
        """Find a Lua object for "name", perhaps using the given module
        and/or classname.  Returns a list of (name, object entry) tuples.
        """
        # skip parens
        if name[-2:] == '()':
            name = name[:-2]

        if not name:
            return []

        objects = self.data['objects']
        matches: List[Tuple[str, Any]] = []

        new_name = None

        # NOTE: searching for exact match, object type is not considered
        if name in objects:
            new_name = name
        elif type == 'mod':
            # only exact matches allowed for modules
            return []
        elif class_name and class_name + '.' + name in objects:
            new_name = class_name + '.' + name
        elif modname and modname + '.' + name in objects:
            new_name = modname + '.' + name
        elif modname and class_name and \
                modname + '.' + class_name + '.' + name in objects:
            new_name = modname + '.' + class_name + '.' + name
        # special case: object methods
        elif type in ('func', 'meth') and '.' not in name and \
                'object.' + name in objects:
            new_name = 'object.' + name

        if new_name is not None:
            matches.append((new_name, objects[new_name]))

        return matches

    def resolve_xref(self, env: BuildEnvironment, from_doc_name: str, builder: Builder,
                     type: str, target: str, node: nodes.Element, cont_node: nodes.Node)-> Optional[nodes.Node]:
        modname = node.get('lua:module')
        class_name = node.get('lua:class')
        search_mode = 0
        matches = self.find_obj(env, modname, class_name, target,
                                type, search_mode)
        if not matches:
            return None
        elif len(matches) > 1:
            logger.warning('more than one target found for cross-reference %r: %s',
                           target, ', '.join(match[0] for match in matches),
                           type='ref', subtype='lua', location=node)

        name, obj = matches[0]

        if obj[1] == 'module':
            return self._make_module_refnode(builder, from_doc_name, name,
                                             cont_node)
        else:
            return make_refnode(builder, from_doc_name, obj[0], name,
                                cont_node, name)

    def resolve_any_xref(self, env: BuildEnvironment, from_doc_name: str, builder: Builder, target: str,
                         node: nodes.Node, cont_node: nodes.Node) -> List[Tuple[str, nodes.Node]]:
        modname = node.get('lua:module')
        class_name = node.get('lua:class')
        results:  List[Tuple[str, nodes.Node]] = []

        # always search in "refspecific" mode with the :any: role
        matches = self.find_obj(env, modname, class_name, target, None, 1)
        for name, obj in matches:
            if obj[1] == 'module':
                results.append(('lua:mod',
                                self._make_module_refnode(builder, from_doc_name,
                                                          name, cont_node)))
            else:
                results.append(('lua:' + self.role_for_objtype(obj[1]),
                                make_refnode(builder, from_doc_name, obj[0], name,
                                             cont_node, name)))
        return results

    def _make_module_refnode(self, builder: Builder, fromdocname: str, name: str, cont_node: nodes.Node) -> nodes.Node:
        # get additional info for modules
        docname, synopsis, platform, deprecated = self.data['modules'][name]
        title = name
        if synopsis:
            title += ': ' + synopsis
        if deprecated:
            title += _(' (deprecated)')
        if platform:
            title += ' (' + platform + ')'
        return make_refnode(builder, fromdocname, docname,
                            'module-' + name, cont_node, title)

    def get_objects(self) -> Iterator[Tuple[str, str, str, str, str, int]]:
        for modname, info in self.data['modules'].items():
            yield (modname, modname, 'module', info[0], 'module-' + modname, 0)
        for refname, (docname, type) in self.data['objects'].items():
            if type != 'module':  # modules are already handled
                yield (refname, refname, type, docname, refname, 1)

    def get_full_qualified_name(self, node: nodes.Element) -> Optional[str]:
        modname = node.get('lua:module')
        class_name = node.get('lua:class')
        target = node.get('reftarget')
        if target is None:
            return None
        else:
            return '.'.join(filter(None, [modname, class_name, target]))


def setup(app):
    app.add_domain(LuaDomain)

    return {
        'version': 'builtin',
        'parallel_read_safe': True,
        'parallel_write_safe': True,
    }
