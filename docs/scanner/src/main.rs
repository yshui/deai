use anyhow::{anyhow, Context};
use std::{
    borrow::Cow,
    cell::{Ref, RefCell, RefMut},
    collections::{BTreeMap, BTreeSet},
    iter::repeat,
    ops::{Deref, DerefMut},
    path::{Path, PathBuf},
    rc::Rc,
};

use clang::{
    documentation::{Comment, CommentChild},
    EntityKind,
};
use clap::Parser;
use std::io::Write;

#[derive(Parser, Debug)]
struct Options {
    input:  PathBuf,
    output: PathBuf,
}

#[derive(Hash, PartialEq, Eq, Clone, Debug, PartialOrd, Ord)]
enum Type {
    Base { namespace: Option<String>, member: String },
    Array(Box<Type>),
}

struct SimpleTypeDisplay<'a>(&'a Type);
impl std::fmt::Display for SimpleTypeDisplay<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self.0 {
            Type::Base { member, .. } => write!(f, "{}", member),
            Type::Array(inner) => {
                write!(f, "[{}]", inner.simple_display())
            }
        }
    }
}

struct RstTypeDisplay<'a>(&'a Type);
impl std::fmt::Display for RstTypeDisplay<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self.0 {
            Type::Base { namespace, member } => {
                if let Some(ns) = namespace {
                    write!(f, "{ns}.", ns = ns)?;
                }
                write!(f, "{member}", member = member)
            }
            Type::Array(inner) => write!(f, "{}", inner.rst_display()),
        }
    }
}

impl Type {
    fn base_type(&self) -> &Type {
        match self {
            Self::Base { .. } => self,
            Self::Array(inner) => inner.base_type(),
        }
    }
    fn namespace(&self) -> Option<&Option<String>> {
        match self {
            Self::Base { namespace, .. } => Some(namespace),
            _ => None,
        }
    }
    fn member(&self) -> Option<&String> {
        match self {
            Self::Base { member, .. } => Some(member),
            _ => None,
        }
    }
    fn simple_display(&self) -> SimpleTypeDisplay {
        SimpleTypeDisplay(self)
    }
    fn rst_display(&self) -> RstTypeDisplay {
        RstTypeDisplay(self)
    }
}

impl std::fmt::Display for Type {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Base { namespace, member } => {
                if let Some(ns) = namespace {
                    write!(f, "{}", ns)?;
                }
                write!(f, ":{}", member)
            }
            Self::Array(inner) => write!(f, "[{}]", inner),
        }
    }
}

#[derive(Debug)]
struct Parameter {
    name: String,
    ty:   Option<Type>,
    doc:  String,
}

impl std::fmt::Display for Parameter {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.name)?;
        if let Some(ty) = &self.ty {
            write!(f, ": {}", ty.simple_display())?;
        }
        Ok(())
    }
}
#[derive(Debug, Default)]
struct Doc {
    /// A brief description
    brief: String,
    /// An array of paragraphs
    body:  Vec<String>,
}
/// How can a type be accessed?
#[derive(Debug, Hash, PartialEq, Eq, Clone, PartialOrd, Ord)]
enum Access<'a> {
    /// As descendent of the root object
    Ancestry { path: Cow<'a, str> },
    /// As member of a type
    Member { ty: Type, member: Cow<'a, str> },
}

impl Access<'_> {
    fn is_ancestry(&self) -> bool {
        match self {
            Self::Ancestry { .. } => true,
            _ => false,
        }
    }
}

impl std::fmt::Display for Access<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Ancestry { path } => write!(f, "{}", path),
            Self::Member { ty, member } => write!(f, "{}.{}", ty, member),
        }
    }
}
#[derive(Debug)]
struct Entry {
    /// How can this export be accessed. If None, this export is accessed indirectly via return
    /// types of methods, or as a property.
    path:     Access<'static>,
    /// List of parameters to the method, `None` if this is a property. bool indicate if there's
    /// '...' at the end.
    params:   Option<(Vec<Parameter>, bool)>,
    ty:       Option<Type>,
    doc:      Option<Doc>,
    children: BTreeMap<String, Rc<RefCell<Entry>>>,
}

struct EntryDisplay<'a>(&'a Entry);

impl std::fmt::Display for EntryDisplay<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.0.path.base_name())?;
        if let Some((params, dots)) = &self.0.params {
            write!(
                f,
                "({}{})",
                params.iter().map(|x| &x.name).join(", "),
                if *dots {
                    if params.len() > 0 {
                        ", ..."
                    } else {
                        "..."
                    }
                } else {
                    ""
                }
            )?;
        }
        Ok(())
    }
}

impl Entry {
    fn display(&self) -> EntryDisplay {
        EntryDisplay(self)
    }
    fn role(&self) -> &'static str {
        if self.params.is_none() {
            "lua:attr"
        } else if self.ty.is_none() {
            "lua:sgnl"
        } else {
            "lua:meth"
        }
    }
    fn directive(&self) -> &'static str {
        if self.params.is_none() {
            "lua:attribute"
        } else if self.ty.is_none() {
            "lua:signal"
        } else {
            "lua:method"
        }
    }
}

#[derive(Default, Debug)]
struct TypeEntry {
    references: BTreeSet<Access<'static>>,
    children:   BTreeMap<String, Rc<RefCell<Entry>>>,
    ancestor:   Option<Access<'static>>,
    doc:        Doc,
}
trait HasChildren {
    fn children(&self) -> &BTreeMap<String, Rc<RefCell<Entry>>>;
}
trait HasChildrenMut: HasChildren {
    fn children_mut(&mut self) -> &mut BTreeMap<String, Rc<RefCell<Entry>>>;
}
impl HasChildren for TypeEntry {
    fn children(&self) -> &BTreeMap<String, Rc<RefCell<Entry>>> {
        &self.children
    }
}
impl HasChildrenMut for TypeEntry {
    fn children_mut(&mut self) -> &mut BTreeMap<String, Rc<RefCell<Entry>>> {
        &mut self.children
    }
}
impl HasChildren for Entry {
    fn children(&self) -> &BTreeMap<String, Rc<RefCell<Entry>>> {
        &self.children
    }
}
impl HasChildrenMut for Entry {
    fn children_mut(&mut self) -> &mut BTreeMap<String, Rc<RefCell<Entry>>> {
        &mut self.children
    }
}
use either::Either;
impl<A, B> HasChildren for Either<A, B>
where
    A: Deref,
    B: Deref,
    <A as Deref>::Target: HasChildren,
    <B as Deref>::Target: HasChildren,
{
    fn children(&self) -> &BTreeMap<String, Rc<RefCell<Entry>>> {
        match self {
            Either::Left(a) => a.children(),
            Either::Right(a) => a.children(),
        }
    }
}
impl<A, B> HasChildrenMut for Either<A, B>
where
    A: DerefMut,
    B: DerefMut,
    <A as Deref>::Target: HasChildrenMut,
    <B as Deref>::Target: HasChildrenMut,
{
    fn children_mut(&mut self) -> &mut BTreeMap<String, Rc<RefCell<Entry>>> {
        match self {
            Either::Left(a) => a.children_mut(),
            Either::Right(a) => a.children_mut(),
        }
    }
}
use itertools::Itertools;
#[derive(Debug)]
enum AccessOrType<'a> {
    Access(Access<'a>),
    Type(&'a Type),
}
impl std::fmt::Display for AccessOrType<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Access(a) => write!(f, "{}", a),
            Self::Type(t) => write!(f, "{}", t),
        }
    }
}
impl Access<'_> {
    fn parent(&self) -> Option<AccessOrType> {
        use utils::namespace::Namespace;
        match self {
            Self::Ancestry { path } => path
                .parent()
                .map(|path| AccessOrType::Access(Access::Ancestry { path: path.into() })),
            Self::Member { ty, .. } => Some(AccessOrType::Type(&ty)),
        }
    }
    fn base_name(&self) -> &str {
        use utils::namespace::Namespace;
        match self {
            Self::Ancestry { path } => path.last(),
            Self::Member { member, .. } => member,
        }
    }
}

#[derive(Debug)]
struct Docs {
    by_access: BTreeMap<Access<'static>, Rc<RefCell<Entry>>>,
    by_type:   BTreeMap<Type, RefCell<TypeEntry>>,
    has_error: bool,
}
mod parsers {
    use std::collections::HashMap;

    use itertools::Itertools;
    use nom::branch::alt;
    use nom::bytes::complete::tag;
    use nom::bytes::complete::take_while1;
    use nom::character::complete::multispace0;
    use nom::character::complete::space0;
    use nom::combinator::map;
    use nom::combinator::opt;
    use nom::error::ParseError;
    use nom::multi::many0;
    use nom::multi::separated_list0;
    use nom::multi::separated_list1;
    use nom::sequence::delimited;
    use nom::sequence::preceded;
    use nom::sequence::separated_pair;
    use nom::sequence::terminated;
    use nom::sequence::tuple;
    use nom::AsChar;
    use nom::Compare;
    use nom::IResult;
    use nom::InputTake;
    use nom::InputTakeAtPosition;

    use crate::Access;

    use super::{Entry, Parameter, Type};

    /// Matches tag ignoring white spaces
    fn tag2<Input, Error: ParseError<Input>>(
        s: &'static str,
    ) -> impl FnMut(Input) -> IResult<Input, Input, Error>
    where
        Input: InputTakeAtPosition + Compare<&'static str> + InputTake,
        <Input as InputTakeAtPosition>::Item: AsChar + Clone,
    {
        delimited(multispace0, tag(s), multispace0)
    }

    fn ident(s: &str) -> IResult<&str, &str> {
        take_while1(|c: char| c == '-' || c == '_' || c.is_alphanumeric())(s)
    }
    fn namespaced_ident(s: &str) -> IResult<&str, String> {
        separated_list1(tag("."), ident)(s).map(|(i, o)| (i, o.into_iter().join(".")))
    }
    fn parameters(s: &str) -> IResult<&str, Vec<Parameter>> {
        let ty = alt((array_type, base_type));
        map(separated_list0(tag2(","), tuple((ident, opt(preceded(tag2(":"), ty))))), |o| {
            o.into_iter()
                .map(|(s, ty)| Parameter { name: s.to_owned(), ty, doc: String::new() })
                .collect()
        })(s)
    }
    fn access(s: &str) -> IResult<&str, Access<'static>> {
        alt((
            map(separated_pair(base_type, tag("."), ident), |(ty, member)| Access::Member {
                ty,
                member: member.to_owned().into(),
            }),
            map(namespaced_ident, |path| Access::Ancestry { path: path.into() }),
        ))(s)
    }
    fn declaration(s: &str) -> IResult<&str, (Access<'static>, Option<(Vec<Parameter>, bool)>)> {
        let param_list = delimited(
            tag2("("),
            tuple((parameters, map(opt(preceded(tag2(","), tag("..."))), |o| o.is_some()))),
            tag2(")"),
        );
        tuple((access, opt(param_list)))(s)
    }
    fn base_type(s: &str) -> IResult<&str, Type> {
        tuple((opt(namespaced_ident), preceded(tag(":"), ident)))(s)
            .map(|(i, (o1, o2))| (i, Type::Base { namespace: o1, member: o2.to_owned() }))
    }
    // No nested array types
    fn array_type(s: &str) -> IResult<&str, Type> {
        delimited(tag2("["), alt((base_type, array_type)), tag2("]"))(s).map(|(i, o)| (i, Type::Array(Box::new(o))))
    }

    fn param_detail_line(s: &str) -> IResult<&str, Parameter> {
        let mut prefix = map(
            preceded(
                tag2("-"),
                tuple((
                    ident,
                    opt(delimited(
                        tag2("("),
                        alt((array_type, base_type)),
                        preceded(multispace0, tag(")")),
                    )),
                )),
            ),
            |(name, ty)| Parameter { name: name.to_owned(), ty, doc: String::new() },
        );
        let (rest, mut param) = prefix(s)?;

        let (rest, _) = space0(rest)?;
        let mut rest = rest.chars();
        while let Some(ch) = rest.next() {
            if ch == '\n' || ch == '\r' {
                // Skip white space after newline
                let (next_ch, _) = multispace0(rest.as_str())?;
                if prefix(next_ch).is_ok() {
                    // We reached the beginning of the next param line
                    break
                }
                param.doc.push(' ');
                rest = next_ch.chars();
            } else {
                param.doc.push(ch);
            }
        }
        Ok((rest.as_str(), param))
    }

    pub(crate) fn param_detail_lines(s: &str) -> IResult<&str, HashMap<String, Parameter>> {
        map(many0(param_detail_line), |lines| {
            lines.into_iter().map(|p| (p.name.clone(), p)).collect()
        })(s)
    }

    pub(crate) fn signal_line(s: &str) -> IResult<&str, Entry> {
        map(preceded(tuple((tag("SIGNAL:"), space0)), declaration), |(access, params)| Entry {
            path: access,
            params,
            ty: None,
            doc: None,
            children: Default::default(),
        })(s)
    }

    pub(crate) fn export_line(s: &str) -> IResult<&str, Entry> {
        preceded(
            tuple((tag("EXPORT:"), space0)),
            tuple((terminated(declaration, tag2(":")), alt((array_type, base_type)))),
        )(s)
        .map(|(i, ((path, params), o2))| {
            (i, Entry { path, params, ty: Some(o2), doc: None, children: Default::default() })
        })
    }

    pub(crate) fn type_line(s: &str) -> IResult<&str, Type> {
        preceded(tuple((tag("TYPE:"), space0)), base_type)(s)
    }
}

mod utils {
    pub(super) mod namespace {
        pub trait Namespace {
            fn is_ancestor(&self, other: &Self) -> bool;
            fn validate(&self) -> bool;
            fn parent(&self) -> Option<&Self>;
            fn last(&self) -> &Self;
        }
        impl Namespace for str {
            /// Check if namespace `a` is a ancestor of namespace `b`
            fn is_ancestor(&self, b: &str) -> bool {
                b.strip_prefix(self).and_then(|rest| rest.chars().next()) == Some('.')
            }

            fn validate(&self) -> bool {
                self.split('.').all(|x| x.len() != 0)
            }

            fn parent(&self) -> Option<&str> {
                self.rsplit_once('.').map(|(a, _)| a)
            }
            fn last(&self) -> &str {
                self.rsplit_once('.').map(|(_, b)| b).unwrap_or(self)
            }
        }
    }
}

impl Docs {
    fn new() -> Self {
        Self { by_access: Default::default(), by_type: Default::default(), has_error: false }
    }
    fn collect_paragraphs(comment: &Comment) -> Vec<String> {
        comment
            .get_children()
            .into_iter()
            .filter_map(|child| match child {
                CommentChild::Paragraph(children) => Some(
                    children
                        .into_iter()
                        .filter_map(|child| match child {
                            CommentChild::Text(txt) => {
                                let stripped =
                                    txt.strip_prefix(" ").map(|x| x.to_owned()).unwrap_or(txt);
                                if stripped.is_empty() {
                                    None
                                } else {
                                    Some(stripped)
                                }
                            }
                            _ => None,
                        })
                        .join("\n")
                        .to_owned(),
                ),
                _ => None,
            })
            .collect()
    }
    fn handle_comment_inner(&mut self, comment: &Comment) -> anyhow::Result<()> {
        let paragraphs = Self::collect_paragraphs(comment);
        let mut brief = None;
        let mut body = vec![];
        let mut entry: Option<Entry> = None;

        let mut save_entry =
            |entry: &mut Option<Entry>, brief: &mut Option<String>, body: &mut Vec<String>| {
                if let Some(mut entry) = entry.take() {
                    log::info!("Added {}", entry.display());
                    let new_body = vec![];
                    let body = std::mem::replace(body, new_body);
                    entry.doc = brief.take().map(|brief| Doc { brief, body });
                    let path = entry.path.clone();
                    let entry = Rc::new(RefCell::new(entry));
                    self.by_access.insert(path, entry.clone());
                }
            };

        let mut paragraphs = paragraphs.into_iter();
        while let Some(p) = paragraphs.next() {
            if p.starts_with("EXPORT:") {
                log::debug!("Export line {}", p);
                let signature = parsers::export_line(&p);
                let signature =
                    signature.map(|(_, a)| a).map_err(|e| anyhow!("Invalid export line: {}", e))?;
                save_entry(&mut entry, &mut brief, &mut body);
                entry = Some(signature);
            } else if p.starts_with("SIGNAL:") {
                log::debug!("Signal line: {}", p);
                let (next, signature) =
                    parsers::signal_line(&p).map_err(|e| anyhow!("Invalid signal line: {}", e))?;
                save_entry(&mut entry, &mut brief, &mut body);
                let next = next.trim().replace(|ch| ch == '\n' || ch == '\r', " ");

                brief = Some(next);
                entry = Some(signature);
            } else if p == "Arguments:" {
                let p = paragraphs.next().context("Arguments: not followed by parameter block")?;
                let (_, mut param_detail) = parsers::param_detail_lines(&p).map_err(|_| {
                    anyhow!("Parameter block not proceeded by a signal or export line")
                })?;
                if let Some(entry) = &mut entry {
                    if let Some((params, _)) = &mut entry.params {
                        for p in params {
                            if let Some(detail) = param_detail.get_mut(&p.name) {
                                if !p.doc.is_empty() || (p.ty.is_some() && detail.ty.is_some()) {
                                    return Err(anyhow!(
                                        "Duplicated documentation for parameter {}",
                                        p.name
                                    ))
                                }
                                p.doc = std::mem::replace(&mut detail.doc, String::new());
                                if p.ty.is_none() {
                                    p.ty = detail.ty.take();
                                }
                            }
                        }
                    } else {
                        return Err(anyhow!("Parameter block attached to a property"))
                    }
                }
            } else {
                if brief.is_none() {
                    brief = Some(p);
                } else {
                    body.push(p);
                }
            }
        }
        save_entry(&mut entry, &mut brief, &mut body);
        Ok(())
    }
    fn handle_comment(&mut self, comment: &Comment) {
        let ret = self.handle_comment_inner(comment);
        if let Err(e) = ret {
            self.has_error = true;
            log::error!("{}", e);
        }
    }
    fn handle_type_comment(&mut self, comment: &Comment) -> Option<&mut RefCell<TypeEntry>> {
        let paragraphs = Self::collect_paragraphs(comment);
        let mut brief = None;
        let mut body = vec![];
        let mut entry = None;
        for p in paragraphs {
            if p.starts_with("TYPE:") {
                if let Ok(ty) = parsers::type_line(&p) {
                    if entry.is_none() {
                        entry = Some(ty.1);
                    } else {
                        self.has_error = true;
                        log::error!("Multiple export lines: {}", p);
                    }
                } else {
                    self.has_error = true;
                    log::error!("Invalid export line: {}", p);
                    return None
                }
            } else {
                if brief.is_none() {
                    brief = Some(p);
                } else {
                    body.push(p);
                }
            }
        }
        entry.map(move |entry| {
            let entry = self.by_type.entry(entry).or_insert_with(Default::default);
            entry.borrow_mut().doc.brief = brief.unwrap_or(String::new());
            entry.borrow_mut().doc.body.extend(body.into_iter());
            entry
        })
    }
    fn with_entry<T>(
        &self,
        key: &AccessOrType,
        f: impl FnOnce(Either<Ref<Entry>, Ref<TypeEntry>>) -> T,
    ) -> Option<T> {
        match &key {
            AccessOrType::Access(access) => {
                self.by_access.get(&access).map(|a| f(Either::Left(a.borrow())))
            }
            AccessOrType::Type(ty) => self.by_type.get(&ty).map(|a| f(Either::Right(a.borrow()))),
        }
    }
    fn with_entry_mut<T>(
        &self,
        key: &AccessOrType,
        f: impl FnOnce(Either<RefMut<Entry>, RefMut<TypeEntry>>) -> T,
    ) -> Option<T> {
        match &key {
            AccessOrType::Access(access) => {
                self.by_access.get(&access).map(|a| f(Either::Left(a.borrow_mut())))
            }
            AccessOrType::Type(ty) => {
                self.by_type.get(&ty).map(|a| f(Either::Right(a.borrow_mut())))
            }
        }
    }
    fn build_tree(&mut self) {
        for (k, v) in self.by_access.iter() {
            if let Some(ty) = &v.borrow().ty {
                let te = self.by_type.entry(ty.base_type().clone()).or_insert_with(|| {
                    log::info!("Added type {}", ty.base_type());
                    Default::default()
                });
                te.borrow_mut().references.insert(k.clone());
                if k.is_ancestry() && v.borrow().params.is_none() {
                    te.borrow_mut().ancestor = Some(k.clone());
                }
            } else {
                if let Some((params, _)) = &v.borrow().params {
                    // Add references for types used in a signal
                    for p in params {
                        if let Some(ty) = &p.ty {
                            let te = self
                                .by_type
                                .entry(ty.base_type().clone())
                                .or_insert_with(Default::default);
                            te.borrow_mut().references.insert(k.clone());
                        }
                    }
                }
            }
        }
        let mut has_error = false;
        for (k, v) in self.by_access.iter() {
            if let Some(parent) = k.parent() {
                let old = self.with_entry_mut(&parent, |mut e| {
                    e.children_mut().insert(k.base_name().to_owned(), v.clone())
                });
                if let Some(old) = old {
                    if let Some(old) = old {
                        log::error!("Duplicated entry {}", old.borrow().path);
                        has_error = true;
                    }
                } else {
                    log::error!("Missing parent {}", parent);
                    has_error = true;
                }
            }
        }
        self.has_error |= has_error;
    }
    fn print_entries<'a, It, Item, W: std::io::Write>(it: It, mut output: W) -> std::io::Result<()>
    where
        It: Iterator<Item = (Item, &'a Rc<RefCell<Entry>>)> + Clone,
        Item: AsRef<str>,
    {
        for (name, child) in it.clone() {
            let name = name.as_ref();
            let child = child.borrow();
            writeln!(
                output,
                "   * - :{}:`{} <{name}>`\n     - {}",
                child.role(),
                child.display(),
                child.doc.as_ref().map(|x| x.brief.as_str()).unwrap_or(""),
                name = name
            )?;
        }
        writeln!(output)?;
        let ref_type = |output: &mut W, ty: &Type| -> std::io::Result<()> {
            let base_type = ty.base_type();
            if base_type.namespace().unwrap().is_none() {
                // A fundamental type
                writeln!(output, "{}", ty.simple_display())?;
            } else {
                writeln!(output, ":lua:mod:`{} <{}>`", ty.simple_display(), ty.rst_display(),)?;
            }
            Ok(())
        };
        for (_, child) in it.clone() {
            let child = child.borrow();
            write!(output, ".. {}:: {}", child.directive(), child.display())?;
            if let Some(ty) = &child.ty {
                writeln!(
                    output,
                    "{} {}\n",
                    if child.params.is_none() { ":" } else { " ->" },
                    ty.simple_display(),
                )?;
                let type_tag = if child.params.is_none() { "type" } else { "rtype" };
                write!(output, "   :{type_tag}: ", type_tag = type_tag)?;
                ref_type(&mut output, child.ty.as_ref().unwrap())?;
            } else {
                writeln!(output, "\n")?;
            }

            // Print parameters

            for p in child.params.as_ref().map(|(a, _)| a.iter()).into_iter().flatten() {
                if !p.doc.is_empty() {
                    writeln!(output, "   :param {}: {}", p.name, p.doc)?;
                }
                if let Some(ty) = &p.ty {
                    write!(output, "   :type {}: ", p.name)?;
                    ref_type(&mut output, ty)?;
                    if p.doc.is_empty() {
                        writeln!(output, "   :param {}:", p.name)?;
                    }
                }
            }
            writeln!(output)?;
            if let Some(doc) = &child.doc {
                writeln!(output, "   {} \n", doc.brief)?;
                for p in &doc.body {
                    writeln!(
                        output,
                        "{}\n",
                        p.split("\n").map(|p| format!("   {}", p)).join("\n")
                    )?;
                }
            }
        }
        Ok(())
    }
    fn generate_references(
        &self,
        type_entry: &TypeEntry,
        mut output: impl std::io::Write,
    ) -> std::io::Result<()> {
        if type_entry.references.len() > 0 {
            let references = type_entry
                .references
                .iter()
                .map(|ref_| match ref_ {
                    a @ Access::Ancestry { .. } => {
                        format!(":lua:meth:`{a} <{a}>`", a = a)
                    }
                    Access::Member { ty, member } => format!(
                        ":lua:attr:`{}.{member} <{}.{member}>`",
                        ty.simple_display(),
                        ty.rst_display(),
                        member = member
                    ),
                })
                .join(", ");
            writeln!(
                output,
                "See {references} for more information about this type\n",
                references = references
            )?;
        }
        Ok(())
    }
    fn generate_single(&self, entry: AccessOrType<'_>, target: &Path) -> std::io::Result<()> {
        self.with_entry(&entry, |e| {
            let children = e.children();
            let mut output = std::fs::File::create(target)?;
            let title = match &e {
                Either::Left(entry) => {
                    assert!(entry.path.is_ancestry());
                    assert!(entry.params.is_none());
                    let path = entry.path.to_string();
                    let title = if entry.ty.as_ref().unwrap().to_string() == "deai:module" {
                        format!("Module {}", path)
                    } else {
                        format!("Type {} ({})", entry.ty.as_ref().unwrap().base_type(), path)
                    };
                    title
                }
                Either::Right(_) => {
                    format!("Type {}", entry)
                }
            };
            writeln!(output, "{}", repeat("=").take(title.len()).join(""))?;
            writeln!(output, "{}", title)?;
            writeln!(output, "{}\n", repeat("=").take(title.len()).join(""))?;

            match &e {
                Either::Left(entry) => {
                    let ty = entry.ty.as_ref().unwrap();
                    if ty.to_string() == "deai:module" {
                        writeln!(output, ".. lua:module:: {}\n", entry.path)?;
                    } else {
                        writeln!(output, ".. lua:module:: {}\n", ty.rst_display())?;
                    }
                    if let Some(doc) = &entry.doc {
                        writeln!(output, "{}\n", doc.brief)?;
                        for p in &doc.body {
                            writeln!(output, "{}\n", p)?;
                        }
                    }
                    if let Some(type_entry) = self.by_type.get(&ty) {
                        self.generate_references(&type_entry.borrow(), &mut output)?;
                    }
                }
                Either::Right(type_entry) => {
                    match entry {
                        AccessOrType::Type(ty) => {
                            writeln!(output, ".. lua:module:: {}\n", ty.rst_display())?
                        }
                        AccessOrType::Access(_) => unreachable!(),
                    }
                    if !type_entry.doc.brief.is_empty() {
                        writeln!(output, "{}\n", type_entry.doc.brief)?;
                        for p in &type_entry.doc.body {
                            writeln!(output, "{}\n", p)?;
                        }
                    }
                    self.generate_references(type_entry, &mut output)?;
                }
            }
            let prop_it = children.iter().filter(|(_, c)| {
                let c = c.borrow();
                c.params.is_none() && c.ty.is_some()
            });
            if prop_it.clone().count() > 0 {
                writeln!(
                    output,
                    "Properties\n==========\n\n.. list-table::\n   :header-rows: 0\n"
                )?;
                Self::print_entries(prop_it, &mut output)?;
            }

            let method_it = children.iter().filter(|(_, c)| {
                let c = c.borrow();
                c.params.is_some() && c.ty.is_some()
            });
            if method_it.clone().count() > 0 {
                writeln!(output, "Methods\n==========\n\n.. list-table::\n   :header-rows: 0\n")?;
                Self::print_entries(method_it, &mut output)?;
            }
            let signal_it = children.iter().filter(|(_, c)| c.borrow().ty.is_none());
            if signal_it.clone().count() > 0 {
                writeln!(output, "Signals\n==========\n\n.. list-table::\n   :header-rows: 0\n")?;
                Self::print_entries(signal_it, &mut output)?;
            }
            Ok(())
        })
        .unwrap()
    }
    fn generate(&self, output: &Path) -> std::io::Result<()> {
        for (access, entry) in &self.by_access {
            if !access.is_ancestry() {
                continue
            }
            if entry.borrow().params.is_some() {
                continue
            }
            self.generate_single(
                AccessOrType::Access(access.clone()),
                &output.join(format!("{}.rst", access)),
            )?;
        }

        for (k, type_entry) in &self.by_type {
            if type_entry.borrow().ancestor.is_some() {
                continue
            }
            if k.namespace().unwrap().is_none() {
                continue
            }
            self.generate_single(AccessOrType::Type(k), &output.join(format!("{}.rst", k)))?;
        }

        // Generate toc files
        let mut types = std::fs::File::create(output.join("types.rst"))?;
        writeln!(types, "=====\nType\n=====\n")?;
        writeln!(types, ".. toctree::\n   :maxdepth: 2\n")?;
        for (k, type_entry) in &self.by_type {
            let ns = k.namespace().unwrap();
            if ns.is_none() || ns.as_ref().map(String::as_str) == Some("deai") {
                continue
            }
            if let Some(ancestor) = &type_entry.borrow().ancestor {
                writeln!(types, "   {k} <{ancestor}>", k = k, ancestor = ancestor)?;
            } else {
                writeln!(types, "   {k} <{k}>", k = k)?;
            }
        }
        // Generate index
        let mut index = std::fs::File::create(output.join("modules.rst"))?;
        writeln!(index, "=============================")?;
        writeln!(index, "Modules and Top-level Methods")?;
        writeln!(index, "=============================\n")?;
        writeln!(index, "\nModules\n=======\n")?;
        writeln!(index, ".. toctree::\n   :maxdepth: 2\n   :hidden:\n")?;
        for (access, entry) in &self.by_access {
            if !access.is_ancestry() {
                continue
            }
            if entry.borrow().params.is_some() {
                continue
            }
            if entry.borrow().ty.as_ref().unwrap().to_string() != "deai:module" {
                continue
            }
            writeln!(index, "   {access} <{access}>", access = access)?;
        }
        writeln!(index, ".. list-table::\n   :header-rows: 0\n")?;
        for (access, entry) in &self.by_access {
            if !access.is_ancestry() || access.parent().is_some() || entry.borrow().params.is_some()
            {
                continue
            }
            writeln!(
                index,
                "   * - :doc:`{access} <{access}>`\n     - {}",
                entry.borrow().doc.as_ref().map(|x| x.brief.as_str()).unwrap_or(""),
                access = access
            )?;
        }
        writeln!(index, "\nMethods\n=======\n")?;

        writeln!(index, ".. list-table::\n   :header-rows: 0\n")?;

        let method_it = self
            .by_access
            .iter()
            .filter(|(k, c)| {
                let c = c.borrow();
                c.params.is_some() && c.ty.is_some() && k.is_ancestry() && k.parent().is_none()
            })
            .map(|(k, v)| (k.to_string(), v));
        Self::print_entries(method_it, index)?;

        Ok(())
    }
}

fn process_child(docs: &mut Docs, child: &clang::Entity) {
    if !child.get_range().map(|x| x.is_in_main_file()).unwrap_or(false) {
        return
    }
    match child.get_kind() {
        EntityKind::FunctionDecl | EntityKind::FieldDecl | EntityKind::Method => {
            if let Some(comment) = child.get_parsed_comment() {
                docs.handle_comment(&comment);
            }
        }
        EntityKind::StructDecl => {
            if let Some(comment) = child.get_parsed_comment() {
                docs.handle_type_comment(&comment);
            }
            for member in child.get_children() {
                log::debug!("{member:?}", member = member);
                process_child(docs, &member);
            }
        }
        EntityKind::Namespace => {
            for child in child.get_children() {
                process_child(docs, &child);
            }
        }
        _ => (),
    }
}

fn is_cpp(cmd: &clang::CompileCommand) -> bool {
    let args = cmd.get_arguments();
    for parts in args.windows(2) {
        if parts[0] == "-x" {
            return parts[1] == "c++"
        }
    }

    let filename = cmd.get_filename();
    if let Some(ext) = filename.extension() {
        return ext == "cpp" || ext == "cxx" || ext == "cc"
    }

    args[0] == "g++" || args[0] == "clang++"
}

#[derive(Debug)]
struct Clang(clang_sys::support::Clang);
impl Clang {
    fn c_include_args(&self) -> impl Iterator<Item = String> + '_ {
        self.0
            .c_search_paths
            .iter()
            .map(|x| x.iter())
            .flatten()
            .map(|x| format!("-I{}", x.display()))
    }
    fn cpp_include_args(&self) -> impl Iterator<Item = String> + '_ {
        self.0
            .cpp_search_paths
            .iter()
            .map(|x| x.iter())
            .flatten()
            .map(|x| format!("-I{}", x.display()))
    }
}

fn main() {
    env_logger::init();
    let clang_sys = Clang(clang_sys::support::Clang::find(None, &[]).unwrap());
    log::info!("{:?}", clang_sys);
    log::info!("clang version {}", clang::get_version());
    let mut docs = Docs::new();
    let args = Options::parse();
    let clang = clang::Clang::new().unwrap();
    let cdb = clang::CompilationDatabase::from_directory(&args.input).unwrap();
    let index = clang::Index::new(&clang, false, true);
    for cmd in cdb.get_all_compile_commands().get_commands() {
        let full_name = cmd.get_directory().join(cmd.get_filename());
        let is_cpp = is_cpp(&cmd);
        log::debug!("{}, argv0: {}, cpp: {is_cpp}", full_name.display(), cmd.get_arguments()[0]);
        let mut parser = index.parser(&full_name);
        parser.skip_function_bodies(true);
        let mut args: Vec<_> = cmd
            .get_arguments()
            .into_iter()
            .skip(1)
            .filter_map(|s| {
                if s.starts_with('-') && s != "-MF" && s != "-MD" && s != "-MQ" && s != "-o" {
                    if s.starts_with("-I") {
                        // canonlize the path
                        let full_include_dir =
                            cmd.get_directory().join(std::path::Path::new(&s[2..]));
                        Some(format!("-I{}", full_include_dir.display()))
                    } else {
                        Some(s)
                    }
                } else {
                    None
                }
            })
            .collect();
        if is_cpp {
            args.extend(clang_sys.cpp_include_args());
        } else {
            args.extend(clang_sys.c_include_args());
        }
        parser.arguments(&args);
        let tu = parser.parse().unwrap();
        //println!("{:?} {:?}", full_name, tu);
        let entity = tu.get_entity();
        //println!("{:?}", entity.get_comment());
        for child in entity.get_children() {
            process_child(&mut docs, &child);
        }
    }
    docs.build_tree();
    if !Path::new(&args.output).exists() {
        std::fs::create_dir(&args.output).unwrap();
    }
    docs.generate(&args.output).unwrap();
}
