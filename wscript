#!/usr/bin/env python
# encoding: utf-8

APPNAME = 'rlncd'
VERSION = '0.1-alpha'
top = '.'
out = 'build'

def options(opt):
    try:
        opt.load('boost')
    except ImportError as e:
        pass

    opt.load('compiler_cxx')
    opt.add_option('--profiler', action='store_true', default=False, help='Enable google CPU profiler')
    opt.add_option('--kodo', action='store', default='../kodo', help='Path to kodo checkout')
    opt.add_option('--kodo_bundle', action='store', default='../kodo/bundle_dependencies', help='Path to kodo dependencies (fifi, sak, etc)')

def configure(cfg):
    #
    # use clang++ by default
    #
    cfg.add_os_flags('CXX')
    if not cfg.env.CXX:
        cfg.env.append_unique('CXX', ['clang++'])

    cfg.load('compiler_cxx boost')

    #
    # kodo
    #
    cfg.start_msg('Checking kodo include path')
    kodo = cfg.path.find_dir(cfg.options.kodo)
    if kodo:
        cfg.env.KODO = kodo.abspath() + '/src'
        cfg.end_msg(cfg.env.KODO)
    else:
        cfg.fatal('Invalid path to kodo' + cfg.options.kodo)

    #
    # kodo bundle dependencies
    #
    bundle = cfg.path.find_dir(cfg.options.kodo_bundle)
    if not bundle:
        cfg.fatal('Invalid bundle path: ' + cfg.options.kodo_bundle)

    #
    # sak
    #
    cfg.start_msg('Checking sak include path')
    sak = bundle.ant_glob('sak*', dir=True)
    if not sak:
        cfg.fatal('sak not found in bundle path: ' + cfg.options.kodo_bundle)
    cfg.env.SAK = sak[-1].abspath() + '/src'
    cfg.end_msg(cfg.env.SAK)

    #
    # fifi
    #
    cfg.start_msg('Checking fifi include path')
    fifi = bundle.ant_glob('fifi*', dir=True)
    if not fifi:
        cfg.fatal('fifi not found in bundle path: ' + cfg.options.kodo_bundle)
    cfg.env.FIFI = fifi[-1].abspath() + '/src'
    cfg.end_msg(cfg.env.FIFI)

    #
    # libs
    #
    cfg.check_boost()
    cfg.check(features='cxx cxxprogram', lib='pthread', uselib_store='pthread')
    cfg.check(features='cxx cxxprogram', lib='rt', uselib_store='rt')
    cfg.check(features='cxx cxxprogram', lib='nl-3', uselib_store='nl-3')
    cfg.check(features='cxx cxxprogram', lib='nl-genl-3', uselib_store='nl-genl-3')
    cfg.check(features='cxx cxxprogram', lib='gflags', uselib_store='gflags')
    cfg.check(features='cxx cxxprogram', lib='glog', uselib_store='glog')
    cfg.check(features='cxx cxxprogram', lib='gtest', uselib_store='gtest')
    if cfg.options.profiler:
        cfg.check(features='cxx cxxprogram', lib='profiler', uselib_store='profiler')

    cfg.env.append_unique('CXXFLAGS', ['-std=c++11', '-g'])
    env = cfg.get_env()

    cfg.setenv('tsan', env)
    cfg.env.append_unique('CXXFLAGS', ['-g', '-O1', '-fsanitize=thread', '-fPIE'])
    cfg.env.append_unique('LINKFLAGS', ['-pie', '-fsanitize=thread'])

    cfg.setenv('asan', env)
    cfg.env.append_unique('CXXFLAGS', ['-fsanitize=address', '-fno-omit-frame-pointer', '-O1', '-g'])
    cfg.env.append_unique('LINKFLAGS', ['-fsanitize=address'])

def build(bld):
    bld(
            includes='./src',
            export_includes='./src',
            name='rlncd_includes'
    )

    bld.recurse('src')
    bld.recurse('test')

from waflib.Build import BuildContext
class debug(BuildContext):
    '''build with thread sanitizer'''
    cmd = 'tsan'
    variant = 'tsan'

class debug(BuildContext):
    '''build with address sanitizer'''
    cmd = 'asan'
    variant = 'asan'
